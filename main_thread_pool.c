// thread pool version
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
#include <signal.h>

#include <arpa/inet.h> // htonl
#include <sys/socket.h> // socket(), bind(), listne()...
#include <netinet/in.h>   // struct sockaddr_in
#include <sys/wait.h> // waitpid
#include <pthread.h>
#include <pthread.h> // pthread_mutex_t
#include <semaphore.h> // sem_t
#define PORT 8080
#define POOL_SIZE   8   // worker thread 數量
#define QUEUE_SIZE  16  // buffer 最多放幾個 fd

typedef struct {
    int buf[QUEUE_SIZE];  // 環形陣列
    int in;               // producer 放的位置
    int out;              // consumer 拿的位置

    pthread_mutex_t mutex;  // 保護 in/out/buf 的存取

    sem_t empty;  // 計數「還有幾個空位」，初始值 = QUEUE_SIZE
    sem_t full;   // 計數「有幾個待處理的 fd」，初始值 = 0
} bounded_buffer_t;

bounded_buffer_t pool;

void pool_init(void) {
    pool.in  = 0;
    pool.out = 0;
    pthread_mutex_init(&pool.mutex, NULL);
    sem_init(&pool.empty, 0, QUEUE_SIZE); // 一開始全空
    sem_init(&pool.full,  0, 0);          // 一開始沒工作
}


// int sscanf(const char *str, const char *format, ...);
// String Scan Formatted => sscanf
// int sscanf(const char *str, const char *format, ...);
// return 成功放入幾個

// 拿到 method ＆ path
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

const char *get_mime_type(const char *path){
    // file extension 副檔名
    const char *ext = strrchr(path, '.');
    if(ext == NULL) return "application/octet-stream";
    if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0) return "text/html";
    if (strcmp(ext, ".jpg")  == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(ext, ".png")  == 0) return "image/png";
    if (strcmp(ext, ".css")  == 0) return "text/css";
    if (strcmp(ext, ".js")   == 0) return "application/javascript";
    if (strcmp(ext, ".txt")  == 0) return "text/plain";
    return "application/octet-stream";  
}

void uri_to_path(const char* uri, char *path, size_t size){
    // 如果是根目錄
    if(strcmp(uri, "/") == 0){
        snprintf(path, size, "./www/index.html");
        return;
    }
    // 防止 path traversal 攻擊
    if(strstr(uri, "..") != NULL){
        path[0] = '\0';
        return;
    }
    // 其他就前面加上 /www
    snprintf(path, size, "./www%s", uri);
}

// 將 uri 轉成 server 的路徑
// 分析路徑再拿到　MIME type，寫好 header
// 將寫好的　header 送出後再送出 body
void serve_static_file(const char *uri, int conn_fd){
    // uri 轉成 server 中的 disk route
    char path[1024];
    uri_to_path(uri, path, sizeof(path));
    if(path[0] == '\0'){
        send_response(conn_fd, 400, "Bad Request", "text/plain", "400 Bad Request");
        return;
    }
    
    // 確認 path 有沒有檔案，是不是 dir
    struct stat st;
    if (stat(path, &st) < 0 || !S_ISREG(st.st_mode)) {
        send_response(conn_fd, 404, "Not Found", "text/plain", "404 Not Found");
        return;
    }

    // 開檔
    int file_fd = open(path, O_RDONLY);
    if(file_fd < 0){
        send_response(conn_fd, 500, "Internal Server Error",
                  "text/plain", "500 Internal Server Error");
        return;
    }

    // 取得 mime Type
    const char *mime = get_mime_type(path);

    // 設定 header
    char header[1024];
    int header_len = snprintf(header, sizeof(header), 
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "\r\n",
        mime, (long)st.st_size
    );

    write(conn_fd, header, header_len);

    // 送 body（zero-copy）
    sendfile(conn_fd, file_fd, NULL, st.st_size);

    close(file_fd);
}

// producer：主迴圈呼叫，把 conn_fd 放進 buffer
void pool_push(int conn_fd) {
    sem_wait(&pool.empty);
    pthread_mutex_lock(&pool.mutex);
    pool.buf[pool.in] = conn_fd;
    pool.in = (pool.in + 1) % QUEUE_SIZE;
    pthread_mutex_unlock(&pool.mutex);
    sem_post(&pool.full);
}

// consumer：worker thread 呼叫，從 buffer 拿出 conn_fd
int pool_pop(void) {
    int conn_fd;
    sem_wait(&pool.full);
    pthread_mutex_lock(&pool.mutex);
    conn_fd = pool.buf[pool.out];
    pool.out = (pool.out + 1) % QUEUE_SIZE;
    pthread_mutex_unlock(&pool.mutex);
    sem_post(&pool.empty);
    return conn_fd;
}

void *worker(void *arg){
    while(1){
        int conn_fd = pool_pop();
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
        // 只先處理 GET Request
        if (strcmp(method, "GET") != 0) {
            send_response(conn_fd, 405, "Method Not Allowed",
                        "text/plain", "405 Method Not Allowed");
            close(conn_fd);
            continue;
        }
        serve_static_file(path, conn_fd);
        close(conn_fd);
    }
    return NULL;
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

    // 預先建立 thread pool
    pool_init();
    pthread_t workers[POOL_SIZE];
    for(int i =0; i < POOL_SIZE; i++){
        pthread_create(&workers[i], NULL, worker, NULL); // 開始執行 worker()，剛開始會被 pool_pop 的 semaphore wait 卡住
        pthread_detach(workers[i]);
    }


    while(1){
        // accept
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

        pool_push(conn_fd);
    }
    return 0;
}

