CC = gcc

CFLAGS = -g -std=c2x -D_POSIX_C_SOURCE=200809L -Icommon -pthread \
	-Wall -Werror -Wextra \
	-Wcast-align -Wconversion -Wfloat-equal -Wformat=2 \
	-Wnull-dereference -Wshadow -Wsign-conversion -Wswitch-enum \
	-Wundef -Wunreachable-code -Wunused -fsanitize=undefined

ifneq ($(shell uname -s),Darwin)
CFLAGS += -fmax-errors=5
endif

.PHONY: all client clean format run run-client

TARGETS = cloudIST client/client

SERVER_OBJS = server/main.o server/parser.o server/datacenter.o \
              server/datacenter_utils.o server/resources.o server/filesystem.o

CLIENT_OBJS = client/client.o client/parser.o client/api.o client/filesystem.o

HEADERS = $(wildcard common/*.h server/*.h client/*.h)

SERVER_ARGS = 1 4 4 2 1 2 reg
CLIENT_ARGS = 1 q1 a1 reg

all: $(TARGETS)

cloudIST: $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ -lm

client: client/client

client/client: $(CLIENT_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

run: cloudIST
	@./cloudIST $(SERVER_ARGS)

run-client: client/client
	@./client/client $(CLIENT_ARGS)

clean:
	rm -f $(SERVER_OBJS) $(CLIENT_OBJS) $(TARGETS)

format:
	@command -v clang-format >/dev/null 2>&1 || { echo "Please install clang-format to run this command"; exit 1; }
	clang-format -i server/*.c server/*.h client/*.c client/*.h common/*.h