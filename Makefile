CC = gcc

CFLAGS = -Wall -Wextra -Wpedantic -std=c11 -g

TARGET = build/http_server

SRC = src/main.c

all: $(TARGET)

$(TARGET): $(SRC)
	mkdir -p build
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

clean:
	rm -rf build

run: $(TARGET)
	./$(TARGET)
