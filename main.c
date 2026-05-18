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

// int sscanf(const char *str, const char *format, ...);
// String Scan Formatted => sscanf
// int sscanf(const char *str, const char *format, ...);
// return 成功放入幾個

// return 0 表示成功，return -1 表示失敗
int parse_request(const char *buf, char *method, char *path){
    if(sscanf(buf, "%15s %1023s", method, path) != 2){
        return -1;
    }
    return 0;
}

void send_response(int fd, int status_code, const char *status_text, const char *content_type, const char *body){
    char header[1024];
    int body_len = strlen(body);
    int header_len = snprintf(header, sizeof(header), 
        "HTTP/1.0 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "\r\n",
        status_code, status_text, content_type, body_len
    );
    write(fd, header, header_len);
    write(fd, body, body_len);
}

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
    
    // bind
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
        char method[16], path[1024];
        // parse 失敗
        if(parse_request(buf, method, path) < 0){
            send_response(conn_fd, 400, "Bad Request", "text/plain", "400 Bad Request");
            close(conn_fd);
            continue;
        }

        if (strcmp(method, "GET") != 0) {
            send_response(conn_fd, 405, "Method Not Allowed",
                        "text/plain", "405 Method Not Allowed");
            close(conn_fd);
            continue;
        }

        send_response(conn_fd, 200, "OK", "text/html", "<h1>Hello from Tiny Server!</h1>");


        close(conn_fd);
    }

    
    return 0;
}