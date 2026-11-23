CC = gcc

CFLAGS = -ggdb -std=c2x -D_POSIX_C_SOURCE=200809L -pthread \
		 -Wall -Werror -Wextra \
		 -Wcast-align -Wconversion -Wfloat-equal -Wformat=2 -Wnull-dereference -Wshadow -Wsign-conversion -Wundef -Wunreachable-code -Wunused \
 		 -fsanitize=undefined

ifneq ($(shell uname -s),Darwin) # if not MacOS
	CFLAGS += -fmax-errors=5
endif

.PHONY: all clean format run

TARGETS = client

all: $(TARGETS)

client: client.c common.h  constants.h filesystem.o parser.o api.o 
	$(CC) $(CFLAGS) -o client client.c filesystem.o parser.o api.o

%.o: %.c %.h
	$(CC) $(CFLAGS) -c ${@:.o=.c}


clean:
	rm -f *.o $(TARGETS) 

format:
	@which clang-format >/dev/null 2>&1 || echo "Please install clang-format to run this command"
	clang-format -i *.c *.h