CC = gcc

CFLAGS = -Wall -Wextra -Wpedantic -std=c11 -g -Iinclude

TARGET = build/http_server

SRC = src/main.c src/server.c src/http.c src/file.c src/mime.c src/io.c src/config.c


all: $(TARGET)

$(TARGET): $(SRC)
	mkdir -p build
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

clean:
	rm -rf build

run: $(TARGET)
	./$(TARGET)
