# Make a peer executable
add_executable(peer errors.cpp peer_main.cpp peer.cpp)
# Add dependencies
target_link_libraries(peer LINK_PUBLIC lite_torrent)
