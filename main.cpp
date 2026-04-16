//g++ main.cpp -o portscanner
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
#include <csignal>
#include <unordered_set>

using namespace std;

bool quick_mode = false;
bool output_open = true;
bool output_closed = true;

std::unordered_set<int> open_sockets;

void signal_handler(int signal) {
    if (signal == SIGINT) {
        std::cout << "\nCleaning up..." << std::endl;

        // Close all open sockets
        for (int sockfd : open_sockets) {
            if (close(sockfd) == -1) {
                std::cerr << "Failed to close socket: " << strerror(errno) << std::endl;
            }
        }

        // Exit gracefully
        exit(0);
    }
}

int create_socket(){
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd == -1){
        cerr << "Failed to create socket: " << strerror(errno) << endl;
        exit(1); 
    }

    return sockfd;
}

int setup_address(struct sockaddr_in &addr, const char *ip, int port){
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if(!inet_pton(AF_INET, ip, &addr.sin_addr)){
        cerr << "Unable to convert ip string to sin_addr: " << strerror(errno) << endl;
        exit(1);
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
        if(output_open) cout << ntohs(addr.sin_port) << " - OPEN" << endl;
        return true;
    }

    if(errno == ECONNREFUSED || errno == ETIMEDOUT || 
        errno == ENETUNREACH || errno == EHOSTUNREACH){
        if(output_closed) cout << ntohs(addr.sin_port) << " - CLOSED/FILTERED" << endl;
        return false;
    }

    if(errno != EINPROGRESS){
        if(output_closed) cerr << "Unexpected error: " << strerror(errno) << endl;
        return false;
    }

    fd_set write_fds;
    FD_ZERO(&write_fds);
    FD_SET(sockfd, &write_fds);
    
    struct timeval timeout;
    if(quick_mode){
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000; // 100ms
    } else {
        timeout.tv_sec = timeout_sec;
        timeout.tv_usec = 0;
    }

    int select_result = select(sockfd + 1, NULL, &write_fds, NULL, &timeout);
    if(select_result <= 0) {
        if(output_closed) cout << ntohs(addr.sin_port) << " - CLOSED/FILTERED" << endl;
        return false;
    }

    int error = 0;
    socklen_t error_len = sizeof(error);
    getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &error_len);
    if(error == 0){
        if(output_open) cout << ntohs(addr.sin_port) << " - OPEN" << endl;
        return true;
    }else{
        if(output_closed) cout << ntohs(addr.sin_port) << " - CLOSED/FILTERED" << endl;
        return false;
    }
}

bool is_ip_valid(const char *ip){
    struct sockaddr_in addr;
    return inet_pton(AF_INET, ip, &addr.sin_addr) == 1;
}

void set_output_mode(const char *mode){
    if(strcmp(mode, "open") == 0){
        output_open = true;
        output_closed = false;
    }else if (strcmp(mode, "closed") == 0){
        output_open = false;
        output_closed = true;
    }else if(strcmp(mode, "all") == 0){
        output_open = true;
        output_closed = true;
    }else{
        cerr << "Error: invalid output mode '" << mode << "'. Accepted modes: 'open', 'closed' or 'all'." << endl;
        exit(1);
    }
}

int main(int argc, char *argv[]) {
    signal(SIGINT, signal_handler);

    const char *ip = "127.0.0.1";
    int start_port = 1;
    int end_port = 1024;
    int timeout = 1;
    
    for(int i = 1; i < argc; i++){
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0){
            cout << "Port scanner tool. Only use with explicit, written authorization from the owner of the server or computer you are scanning. Otherwise it is a criminal activity.\n" << endl;
            cout << "-h --help\t\tDisplays manual." << endl;
            cout << "-p\t\tDefine ports to be scanned. Can be a single port or a range (e.g. 20-25). Default: 1-1024." << endl;
            cout << "-d\t\tDefine domain to be scanned. Only ip addresses are accepted (e.g. 127.0.0.1). Default: 127.0.0.1." << endl;
            cout << "-q\t\tEnables quickscan. May not be accurate but gets you a quick overview of quickly discoverable open ports. Further scanning may be required. Default: disabled." << endl;
            cout << "-o\t\tOutput mode. Only accepts 'open', 'closed' or 'all'. Determines which information is shown in the output. Default: all." << endl;
            cout << "-t\t\tDefine a custom timeout period. Increasing this value increases reliability at the cost of speed. This value is ignored in quickscan mode. Default: 1.\n" << endl;
            cout << "Example: portscanner -q -p 20-25 -d 192.168.2.48 -o open -t 5" << endl;
            return 1;
        }else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc){
            ip = argv[++i];
            if (!is_ip_valid(ip)){
                cerr << "Invalid IP address" << endl;
                return 1;
            } 
        }else if(strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            if (strchr(argv[i+1], '-')) {
                int start, end;
                if(sscanf(argv[++i], "%d-%d", &start, &end) != 2){
                    cerr << "Invalid input. Try again." << endl;
                    return 1;
                }
                start_port = start;
                end_port = end;
            }else{
                char *endptr;
                long port_num = strtol(argv[i+1], &endptr, 10);

                if(*endptr != '\0'){
                    cerr << "Error: '" << argv[i+1] << "' is not a valid port number." << endl;
                    return 1;
                }
                if(port_num < 1 || port_num > 65535){
                    cerr << "Error: port must be between 1 and 65535." << endl;
                    return 1;
                }
                start_port = end_port = port_num;
            }
            if(start_port < 1 || start_port > 65535 || end_port < 1 || end_port > 65535 || start_port > end_port){
                cerr << "Error: invalid port range: " << to_string(start_port) << "-" << to_string(end_port) << ". Must be 1-65535." << endl;
                return 1;
            }
        }else if(strcmp(argv[i], "-q") == 0) {
            quick_mode = true;
        }else if(strcmp(argv[i], "-o" ) == 0 && i + 1 < argc){
            set_output_mode(argv[++i]);
        }else if(strcmp(argv[i], "-t" ) == 0 && i + 1 < argc){
            char *endptr;
            long timeout_num = strtol(argv[i+1], &endptr, 10);

            if(*endptr != '\0'){
                cerr << "Error: '" << argv[i+1] << "' is not a timeout parameter." << endl;
                return 1;
            }
            if(timeout_num < 1 || timeout_num > 999){
                cerr << "Error: timeout must be between 1 and 999." << endl;
                return 1;
            }
            timeout = timeout_num;
        }
    }

    if(quick_mode) cout << "Starting quickscan..." << endl;
    if(!quick_mode) cout << "Starting scan..." << endl;

    for (int port = start_port; port <= end_port; port++){
        //Create socket
        int sockfd = create_socket();
        open_sockets.insert(sockfd);
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
        connect_with_timeout(sockfd, addr, timeout);

        //Close socket
        if(close(sockfd) == -1) {
            cerr << "Failed to close socket: " << strerror(errno) << endl;
        }
        open_sockets.erase(sockfd);
    }

    return 0;
}