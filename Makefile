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

test: $(TARGET)
	./$(TARGET) &
	server_pid=$$!; \
 	trap 'kill $$server_pid 2>/dev/null || true' EXIT INT TERM; \
	sleep 0.2;\
	python3 tests/test_http.py \
	test_status=$$?; \
	python3 tests/test_limits.py || test_status=$$?; \
	exit $$test_status
