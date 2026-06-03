# Concurrent Tiny Web Server

用 C 語言從零實作的輕量級並發 Web 伺服器，參考 CSAPP Ch.11-12，依序實作五種架構並進行效能比較。

## 專案資訊

- **語言**：C（Linux 原生）
- **參考**：Computer Systems: A Programmer's Perspective (CSAPP) Ch.11、Ch.12
- **測試環境**：
  - Server：Raspberry Pi 4（ARM64）
  - Client：筆電透過 WiFi 連線

---

## 五種架構

### Phase 1：Sequential（單執行緒阻塞）

```c
while(1)
  accept() → read() → parse() → serve() → close()
```

一次只處理一個連線，處理完才接受下一個。實作最單純，但高並發下完全阻塞。

### Phase 2a：Multi-Process（fork）

```c
accept() → fork()
  child：serve() → exit()
  parent：close(conn_fd) → 繼續 accept()
```

每個連線 fork 一個 child process 處理。process 間記憶體完全隔離，但 fork 成本高，需要 `SIGCHLD` + `waitpid()` 回收 zombie。

### Phase 2b：Multi-Thread（pthread）

```c
accept() → pthread_create(handle_request)
  thread：serve() → close() → return
  main：pthread_detach() → 繼續 accept()
```

每個連線建立一個 thread，共享記憶體空間，建立成本低於 fork。conn_fd 與 path 透過 heap 上的 struct 傳入，避免 stack race condition。

### Phase 3：Thread Pool + Bounded Buffer

```c
啟動時建立固定 8 個 worker thread
main：accept() → pool_push(conn_fd)
worker：pool_pop() → read() → parse() → serve() → close()
```

預先建好固定數量的 thread 重複使用，搭配 semaphore 控制的 bounded buffer（容量 16）實現producer/consumer model，避免無限建立 thread 耗盡資源。

### Phase 4：epoll 事件驅動

```c
epoll_create1() → epoll_ctl(ADD, server_fd)
while(1)
  epoll_wait() → for 每個事件:
    server_fd：accept() → epoll_ctl(ADD, conn_fd)
    conn_fd：  read() → parse() → serve() → epoll_ctl(DEL) → close()
```

單執行緒監控所有 fd，有事件才處理。不阻塞在任何 I/O 操作，複雜度 O(就緒事件數)，高並發下資源消耗最低。

---

## 效能比較

### 本機測試（loopback，無網路延遲）

| 版本 | c=1 (req/s) | c=10 (req/s) | c=100 (req/s) |
|------|------------|-------------|--------------|
| sequential | 22,612 | 44,482 | ❌ hang |
| process | 4,984 | 10,045 | 10,896 |
| thread | 8,553 | 26,411 | ❌ hang |
| thread pool | 8,437 | 25,800 | ❌ hang |
| **epoll** | **19,807** | **29,837** | **28,258** |

### 遠端測試（RPi 4 透過 WiFi，有真實網路延遲）

| 版本 | c=1 (req/s) | c=10 (req/s) | c=100 (req/s) |
|------|------------|-------------|--------------|
| sequential | 60.90 | 310.11 | ❌ hang |
| process | 40.21 | 233.92 | ❌ hang |
| thread | 33.38 | 231.82 | ❌ hang |
| thread pool | 37.49 | 202.51 | ❌ hang |
| **epoll** | **44.03** | **275.63** | **249.29** |

### 觀察

1. **c=1 時 sequential 最快**：沒有並發壓力時，sequential 沒有任何 thread/process 管理 overhead，反而最輕量。

2. **只有 epoll 能撐過 c=100**：其他版本在高並發下全部 hang（listen backlog 不足或 bounded buffer 滿溢）。epoll 從 c=10 到 c=100 衰退不到 10%，驗證事件驅動架構的穩定性。

3. **本機 vs 遠端差距約 100 倍**：本機 loopback 測量的是程式碼效率，遠端 WiFi 測量的是有真實網路延遲時的吞吐量，兩者意義不同，需分開解讀。

---

## 各版本 hang 原因分析

| 版本 | c=100 hang 原因 |
|------|----------------|
| sequential | `listen(fd, 10)` backlog 只有 10，超過直接拒絕連線 |
| thread | 系統 thread 數量上限或 backlog 不足 |
| thread pool | `QUEUE_SIZE=16`，buffer 滿後 `pool_push()` 阻塞主迴圈，無法繼續 accept |

---

## 建置與執行

```bash
# 安裝依賴（Debian/Ubuntu/RPi OS）
sudo apt install gcc make

# Clone 專案
git clone https://github.com/alvin119/tiny-concurrent-wev-server-in-c
cd tiny-server

# 編譯所有版本
make

# 執行（選一個版本）
./tiny_sequential
./tiny_process
./tiny_thread
./tiny_pool
./tiny_epoll

# 預設 port：8080
# 靜態檔案放在 ./www/ 目錄下
```

---

## CSAPP 對應章節

| Phase | 對應章節 |
|-------|---------|
| Phase 1（Sequential） | Ch.11 Network Programming |
| Phase 2（fork / pthread） | Ch.12 Concurrent Programming |
| Phase 3（Thread Pool） | Ch.12 Semaphore、Mutex、Producer-Consumer |
| Phase 4（epoll） | man epoll(7)、Linux Programming Interface Ch.63 |

---

## 未來延伸方向

- **Edge Trigger 模式**：將 epoll 從 LT 改為 ET，搭配 non-blocking socket，進一步減少事件通知次數
- **HTTP/1.1 keep-alive**：目前使用 HTTP/1.0，每次請求都重建連線；改為 keep-alive 可大幅減少 TCP handshake 開銷
- **epoll + Thread Pool 混合架構**：epoll 負責 I/O 事件分派，thread pool 負責實際處理，兼顧高並發與多核心利用率
- **動態內容支援（CGI）**：實作 CGI fork 機制，支援執行外部程式生成動態回應
- **wrk / wrk2 壓測**：用 wrk 取代 ab，支援 Lua 腳本自訂請求，取得更精確的延遲分布數據

---

## 專案結構

```
tiny-server/
├── main_sequential.c   # Phase 1：單執行緒
├── main_process.c      # Phase 2a：fork
├── main_thread.c       # Phase 2b：pthread
├── main_thread_pool.c  # Phase 3：thread pool + bounded buffer
├── main.c              # Phase 4：epoll
├── Makefile
└── www/
    ├── index.html
    └── test.JPG
```