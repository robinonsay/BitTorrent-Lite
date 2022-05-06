#ifndef PEER_H
#define PEER_H

#include "ltdefs.h"
#include "mutex/mrsw_mutex.h"
#include "tcp.h"

#include <atomic>
#include <iostream>
#include <list>
#include <map>
#include <netinet/in.h>
#include <thread>

#define PEER_TIMEOUT_ms 30000

typedef std::list<std::thread> ThreadList;
typedef std::map<uint32_t, ChunkHeader> HashChunkHMap;
typedef std::map<uint32_t, ChunkHeader> IndexChunkHMap;
typedef std::list<ChunkHeader> ChunkHList;

class Peer{
    private:
        ChunkHList torrent;
        const char *myIP;
        std::atomic<bool> closing;
        std::ostream& log;
        std::ostream& out;
        TCPClient hub;
        TCPServer peerServer;
        ThreadList threads;
        AddrChunkMap peerCHMap;
        MRSWMutex pchmMtx;
        int reqTorrent();
        void connHandler(sockaddr_in peerAddr);
        void parseTorrent(Packet *pkt);
        void server();
        void update(Packet *pkt);
    public:
        Peer(const char myIP[], const char hubIP[], std::ostream& out_s, std::ostream& log_s=std::cout);
        ~Peer();
        void close(bool interrupt=false);
        void open();
        void run();
};

#endif
