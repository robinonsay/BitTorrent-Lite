#include "tracker.h"
#include "crc32.h"
#include "btldefs.h"
#include "errors.h"
#include "PacketHeader.h"

#include <string>
#include <fstream>
#include <list>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <thread>
#include <poll.h>

Tracker::Tracker(char *pListPath, char *tFilePath, char *inFilePath, std::ofstream *log){
    int status;
    std::string ip_str;
    std::ofstream tFile (tFilePath, std::ofstream::out);
    std::ifstream peersList (pListPath);
    std::ifstream inFile (inFilePath, std::ifstream::binary);
    IP_ADDR peer_addr;
    CHUNK chunk;
    char chunkBuff[CHUNK_SIZE];
    this->log = log;
    if(!peersList.is_open()){
        printf("Peers list file does not exist\n");
        exit(1);
    }
    if(!tFile.is_open()){
        printf("Torrent file could not be opened. Check path\n");
        exit(1);
    }
    if(!inFile.is_open()){
        printf("Input file does not exist\n");
        exit(1);
    }
    peer_addr.sin_family = AF_INET;
    peer_addr.sin_port = htons(TRACKER_PORT);
    while(peersList.good()){
        peersList >> ip_str;
        status = inet_aton(ip_str.c_str(), &peer_addr.sin_addr);
        if(status == 0){
            fprintf(stderr, "ERROR invalid IP address\n");
            exit(1);
        }
        this->ipAddrs.push_back(peer_addr);
    }
    tFile << this->ipAddrs.size() << std::endl;
    for(std::list<IP_ADDR>::iterator it=this->ipAddrs.begin(); it != this->ipAddrs.end(); ++it){
        tFile << inet_ntoa(it->sin_addr) << std::endl;
    }
    for(int i=0; inFile.good(); i++){
        inFile.read(chunkBuff, CHUNK_SIZE);
        if(inFile.fail() && !inFile.eof()){
            fprintf(stderr, "ERROR could not read input file\n");
            exit(1);
        }
        chunk.index = i;
        chunk.hash = crc32(chunkBuff, CHUNK_SIZE);
        this->chunks.push_back(chunk);
    }
    tFile << this->chunks.size() << std::endl;
    for(std::list<CHUNK>::iterator it=this->chunks.begin(); it != this->chunks.end(); ++it){
        tFile << it->index << ' ' << it->hash << std::endl;
    }
    tFile.close();
    peersList.close();
    inFile.close();
    std::ifstream trrntFile (tFilePath);
    memset((char *) &this->trrntPkt, 0, sizeof(this->trrntPkt));
    this->trrntPkt.ph.type = TrrntFileResp;
    trrntFile.read((char *) &this->trrntPkt.payload, sizeof(this->trrntPkt.payload));
    this->trrntPkt.ph.length = trrntFile.gcount();
    this->sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(this->sockfd < 0) sysError("ERROR opening socket");
    memset(&this->tracker_addr, 0, sizeof(this->tracker_addr));
    this->tracker_addr.sin_family = AF_INET;
    this->tracker_addr.sin_port = htons(TRACKER_PORT);
    this->tracker_addr.sin_addr.s_addr = INADDR_ANY;
    status = bind(this->sockfd, (struct sockaddr *) &this->tracker_addr, sizeof(this->tracker_addr));
    if(status < 0) sysError("ERROR on binding");
}

Tracker::~Tracker(){
    if(close(this->sockfd) < 0) sysError("ERROR closing socket");
    for(std::list<std::thread>::iterator th=this->threads.begin(); th != this->threads.end(); ++th){
        th->join();
    }
    printf("\nTracker Server Closed\n");
}

void Tracker::run(){
    int connSockfd, cliAddrLen, status;
    IP_ADDR cliAddr;
    cliAddrLen = sizeof(cliAddr);
    status = listen(this->sockfd, BACKLOG_QUEUE_SIZE);
    if(status < 0) sysError("ERROR on listen");
    printf("Listening on port %d\n", TRACKER_PORT);
    while(1){
        connSockfd = accept(this->sockfd, (struct sockaddr *) &cliAddr, (socklen_t *) &cliAddrLen);
        if(connSockfd < 0) sysError("ERROR accepting connection");
        this->threads.push_back(std::thread(&Tracker::connHandler, this, connSockfd, cliAddr));
    }
}

void Tracker::connHandler(int sockfd, IP_ADDR cliAddr){
    int status, numEvents;
    const int SIZE = 1;
    bool timeout = false;
    bool recvPkt = false;
    struct pollfd pfds[SIZE];
    PACKET_HEADER recvHeader;
    pfds[0].fd = sockfd;
    pfds[0].events = POLLIN;
    memset((char *) &recvHeader, 0, sizeof(recvHeader));
    printf("Accepting connection from %s\n", inet_ntoa(cliAddr.sin_addr));
    while(!(timeout || recvPkt)){
        numEvents = poll(pfds, SIZE, TIMEOUT_ms);
        if(numEvents < 0){
            sysError("ERROR polling cli socket");
        } else if(numEvents == 0){
            // timeout
            timeout = true;
        }else if((pfds[0].revents & POLLIN) == 1){
            // Data on fd
            status = read(sockfd, (char *) &recvHeader, sizeof(recvHeader));
            if(status < 0) sysError("ERROR reading cli socket");
            recvPkt = recvHeader.type == TrrntFileReq && recvHeader.length == 0;
            if(recvPkt){
                status = write(sockfd, &this->trrntPkt, sizeof(this->trrntPkt));
                if(status < 0) sysError("ERROR writing to cli socket");
                printf("Writing torrent file to %s\n", inet_ntoa(cliAddr.sin_addr));
            }
        }
    }
    status = close(sockfd);
    if(status < 0) sysError("ERROR closing cli socket");
    printf("Closing connection to %s\n", inet_ntoa(cliAddr.sin_addr));
}

