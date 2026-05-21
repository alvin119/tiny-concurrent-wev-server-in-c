/*
client: getaddrinfo => socket => connect
server: getaddrinfo => socket => bind => listen => accept
*/ 

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <fcntl.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define PORT 8080

int parse_request(const char *buf, char *method, char *path){
    if(sscanf(buf, "%15s %1023s", method, path) != 2) return -1;
    return 0;
}

void send_response(int fd, int status_code, const char *status_text,
                   const char *content_type, const char *body){
    char header[1024];
    int body_len = strlen(body);
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.0 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "\r\n",
        status_code, status_text, content_type, body_len);
    write(fd, header, header_len);
    write(fd, body, body_len);
}

const char *get_mime_type(const char *path){
    const char *ext = strrchr(path, '.');
    if(ext == NULL) return "application/octet-stream";
    if(strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0) return "text/html";
    if(strcmp(ext, ".jpg")  == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
    if(strcmp(ext, ".png")  == 0) return "image/png";
    if(strcmp(ext, ".css")  == 0) return "text/css";
    if(strcmp(ext, ".js")   == 0) return "application/javascript";
    if(strcmp(ext, ".txt")  == 0) return "text/plain";
    return "application/octet-stream";
}

void uri_to_path(const char *uri, char *path, size_t size){
    if(strcmp(uri, "/") == 0){ snprintf(path, size, "./www/index.html"); return; }
    if(strstr(uri, "..") != NULL){ path[0] = '\0'; return; }
    snprintf(path, size, "./www%s", uri);
}

void serve_static_file(const char *uri, int conn_fd){
    char path[1024];
    uri_to_path(uri, path, sizeof(path));
    if(path[0] == '\0'){
        send_response(conn_fd, 400, "Bad Request", "text/plain", "400 Bad Request");
        return;
    }
    struct stat st;
    if(stat(path, &st) < 0 || !S_ISREG(st.st_mode)){
        send_response(conn_fd, 404, "Not Found", "text/plain", "404 Not Found");
        return;
    }
    int file_fd = open(path, O_RDONLY);
    if(file_fd < 0){
        send_response(conn_fd, 500, "Internal Server Error",
                      "text/plain", "500 Internal Server Error");
        return;
    }
    const char *mime = get_mime_type(path);
    char header[1024];
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "\r\n",
        mime, (long)st.st_size);
    write(conn_fd, header, header_len);
    sendfile(conn_fd, file_fd, NULL, st.st_size);
    close(file_fd);
}

int main(){
    printf("Starting sequential server on port: %d\n", PORT);
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(server_fd < 0){ perror("socket"); exit(1); }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(PORT);
    if(bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0){ perror("bind"); exit(1); }
    if(listen(server_fd, 10) < 0){ perror("listen"); exit(1); }

    while(1){
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int conn_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if(conn_fd < 0){ perror("accept"); continue; }

        char buf[4096];
        memset(buf, 0, sizeof(buf));
        read(conn_fd, buf, sizeof(buf) - 1);

        char method[16], path[1024];
        if(parse_request(buf, method, path) < 0){
            send_response(conn_fd, 400, "Bad Request", "text/plain", "400 Bad Request");
            close(conn_fd); continue;
        }
        if(strcmp(method, "GET") != 0){
            send_response(conn_fd, 405, "Method Not Allowed",
                          "text/plain", "405 Method Not Allowed");
            close(conn_fd); continue;
        }

        serve_static_file(path, conn_fd);  // 單執行緒：處理完才能接下一個
        close(conn_fd);
    }
    return 0;
}