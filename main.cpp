#include <iostream>
#include <cstring>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/select.h>

using namespace std;

int create_socket(){
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd == -1){
        cerr << "Failed to create socket: " << strerror(errno) << endl;
        return -1;  
    }

    return sockfd;
}

int setup_address(struct sockaddr_in &addr, const char *ip, int port){
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if(!inet_pton(AF_INET, ip, &addr.sin_addr)){
        cerr << "Unable to convert ip string to sin_addr: " << strerror(errno) << endl;
        return -1; 
    }
    return 1;
}

bool connect_with_timeout(int sockfd, struct sockaddr_in &addr, int timeout_sec){
    int flags = fcntl(sockfd, F_GETFL, 0);
    if(flags == -1){
        cerr << "fcntl(F_GETFL) failed: " << strerror(errno) << endl;
        return false;
    }
    if(fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == -1) {
        cerr << "fcntl(F_SETFL) failed: " << strerror(errno) << endl;
        return false;
    }

    int connect_result = connect(sockfd, (struct sockaddr*)&addr, sizeof(addr));
    if(connect_result == 0){
        cout << ntohs(addr.sin_port) << " - OPEN" << endl;
        return true;
    }
    if(errno != EINPROGRESS){
        cerr << "Invalid address: " << strerror(errno) << endl;
        return false;
    }

    fd_set write_fds;
    FD_ZERO(&write_fds);
    FD_SET(sockfd, &write_fds);

    struct timeval timeout;
    timeout.tv_sec = timeout_sec;
    timeout.tv_usec = 0;

    int select_result = select(sockfd + 1, NULL, &write_fds, NULL, &timeout);
    if(select_result <= 0) {
        cout << ntohs(addr.sin_port) << " - CLOSED/FILTERED" << endl;
        return false;
    }

    int error = 0;
    socklen_t error_len = sizeof(error);
    getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &error_len);
    if(error == 0){
        cout << ntohs(addr.sin_port) << " - OPEN" << endl;
        return true;
    }else{
        cout << ntohs(addr.sin_port) << " - CLOSED/FILTERED" << endl;
        return false;
    }
}

bool is_ip_valid(const char *ip){
    struct sockaddr_in addr;
    return inet_pton(AF_INET, ip, &addr.sin_addr) == 1;
}

int main(int argc, char *argv[]) {
    const char *ip = "127.0.0.1";
    int start_port = 1;
    int end_port = 1024;
    
    for(int i = 1; i < argc; i++){
        if (strcmp(argv[i], "-d") == 0 && i + 1 < argc){
            ip = argv[++i];
            if (!is_ip_valid(ip)){
                cerr << "Invalid IP address" << endl;
                return 1;
            } 
        }else if(strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            if (strchr(argv[i+1], '-')) {
                sscanf(argv[++i], "%d-%d", &start_port, &end_port);
            }else{
                start_port = end_port = atoi(argv[++i]);
            }
        }
    }

    for (int port = start_port; port <= end_port; port++){
        if(start_port < 1 || start_port > 65535 || end_port < 1 || end_port > 65535 || start_port > end_port){
            cerr << "Error: invalid port range. Must be 1-65535." << endl;
            return 1;
        }
        //Create socket
        int sockfd = create_socket();
        if(sockfd == -1) continue;

        //Setup addr
        sockaddr_in addr;
        if(setup_address(addr, ip, port) == -1){
            if(close(sockfd) == -1) {
                cerr << "Failed to close socket: " << strerror(errno) << endl;
            } 
            continue;
        }

        //Connect
        connect_with_timeout(sockfd, addr, 1);

        //Close socket
        if(close(sockfd) == -1) {
            cerr << "Failed to close socket: " << strerror(errno) << endl;
        } 
    }

    return 0;
}