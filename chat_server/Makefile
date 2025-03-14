# Makefile para el servidor de chat

CC = gcc

# Si estamos en Windows (la variable OS se define en Windows)
ifeq ($(OS),Windows_NT)
    CFLAGS = -Wall -Iinclude
    LIBS = -lws2_32 -lpthread
else
    CFLAGS = -Wall -pthread -Iinclude
    LIBS =
endif

SRC = $(wildcard src/*.c)
OBJ = $(SRC:.c=.o)
TARGET = build/chat_server

all: $(TARGET)

$(TARGET): $(OBJ)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJ) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f src/*.o
	rm -rf build

.PHONY: all clean
