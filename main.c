/*
client: getaddrinfo => socket => connect
server: getaddrinfo => socket => bind => listen => accept
*/ 

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h> // htonl
#include <sys/socket.h> // socket(), bind(), listne()...
#include <netinet/in.h>   // struct sockaddr_in

#define PORT 8080
int main(){
    printf("Starting server on port: %d\n", PORT);
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(server_fd < 0){
        perror("socket");
        exit(1);
    }
    printf("Socket created, fd = %d\n", server_fd);    
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr;
    // void *memset(void *ptr, int value, size_t num); ptr 指向要設定的 memory 起始位置
    memset(&addr, 0, sizeof(addr)); // 將 struct sockaddr_in 裡沒用到的欄位都設為0
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;   // 監聽所有網路介面
    addr.sin_port = htons(PORT);  // host byte order → network byte order
    if(bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0){
        perror("bind");
        exit(1);
    }
    printf("Bind ok\n");
    
    //listen
    if(listen(server_fd, 10) < 0){
        perror("listen");
        exit(1);
    }
    // accept
    while(1){
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int conn_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        // accept()會把連到線的 client IP addr 和 Port 存到 client_addr memory
        // 然後把寫入了多少 bytes 寫到 client_len
        // 因此 client_addr, client_len 放入時都要取址
        if(conn_fd < 0){
            perror("accept");
            continue;
        }
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        printf("Connection from %s:%d\n", client_ip, ntohs(client_addr.sin_port));

        char buf[4096];
        memset(buf, 0, sizeof(buf));
        // read() 是 POSIX 系統呼叫，用來從：檔案, socket, pipe, 裝置讀取資料。
        // ssize_t read(int fd, void *buf, size_t count);
        ssize_t n = read(conn_fd, buf, sizeof(buf) - 1); // -1 是因為 C 字串最後需要一個 '\0'
        if (n > 0) {
            printf("---- Raw Request (%zd bytes) ----\n", n);
            printf("%s", buf);
            printf("---------------------------------\n");
        }

        // parse http Request
        char method[16], path[1024], version[16];
        sscanf(buf, "%s %s %s", method, path, version);
        printf("Method: %s | Path: %s | Version: %s\n", method, path, version);
        close(conn_fd);
    }

    
    return 0;
}