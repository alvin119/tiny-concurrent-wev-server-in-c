CC = gcc
CFLAGS = -Wall -Wextra -O2 -pthread

TARGET = tiny_server
SRCS = main.c
OBJS = $(SRCS:.c=.o) # 把 SRCS 裡面所有 .c 結尾的字串，替換成 .o，這裡只有 main.o

all: $(TARGET)


# 目標 (Target): 依賴項目 (Dependencies)
# [Tab鍵] 執行的終端機指令 (Command)
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^
# $@：代表「目標」的名字（也就是 tiny_server）。
# $^：代表「所有依賴項目」的名字（也就是 main.o）。
# 所以這行展開來其實就是：gcc -Wall -Wextra -O2 -pthread -o tiny_server main.o。

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@
# gcc -Wall -Wextra -O2 -pthread -c main.c -o main.o
clean:
	rm -f $(OBJS) $(TARGET)