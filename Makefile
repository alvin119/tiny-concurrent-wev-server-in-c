CC     = gcc
CFLAGS = -Wall -Wextra -O2 -pthread

TARGETS = tiny_sequential tiny_process tiny_thread tiny_pool tiny_epoll

all: $(TARGETS)

tiny_sequential: main_sequential.c
	$(CC) $(CFLAGS) -o $@ $^

tiny_process: main_process.c
	$(CC) $(CFLAGS) -o $@ $^

tiny_thread: main_thread.c
	$(CC) $(CFLAGS) -o $@ $^

tiny_pool: main_thread_pool.c
	$(CC) $(CFLAGS) -o $@ $^

tiny_epoll: main.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(TARGETS)