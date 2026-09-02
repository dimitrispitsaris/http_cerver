# C HTTP Server

A lightweight HTTP/1.1 server implemented from scratch in C on Linux, built to explore systems programming, POSIX APIs, TCP/IP networking, HTTP protocol handling, filesystem security, process-based concurrency, and event-driven I/O.

The project is developed incrementally, with each architectural stage tested and documented. The final goal is not only to build a functioning HTTP server, but to study and compare two different approaches to concurrent network programming on Linux:

* **Process-per-connection concurrency using ****`fork()`**
* **Event-driven concurrency using non-blocking sockets and Linux ****`epoll`**

Both implementations will provide the same HTTP functionality and will be evaluated under comparable workloads.

---

## Project Goals

This project is primarily a systems-programming exercise in C and Linux.

The main goals are to understand and demonstrate:

* C programming and manual resource management
* POSIX system calls and APIs
* Linux file descriptors
* TCP/IP socket programming
* HTTP/1.0 and HTTP/1.1 request handling
* Persistent HTTP connections
* Filesystem I/O and secure path resolution
* Process-based concurrency with `fork()`
* Non-blocking I/O
* Event-driven programming with `epoll`
* Connection state management
* Defensive error handling
* Automated testing
* Performance measurement and benchmarking

The project is intentionally developed from low-level primitives rather than using an existing HTTP server library.

---

## Current Status

### Implemented

* TCP socket creation, binding, listening, and connection acceptance
* HTTP request parsing
* HTTP/1.0 and HTTP/1.1 handling
* `GET` and `HEAD`
* Static file serving
* MIME type detection
* HTTP error responses
* Persistent HTTP connections / keep-alive
* Request and header size limits
* Client receive timeout
* Secure document-root resolution
* Linux `sendfile()` for file transmission
* Process-based concurrency using `fork()`
* Child-process cleanup with `SIGCHLD` / `waitpid()`
* Automated Python integration tests

### In Development

* Explicit concurrency stress tests
* Benchmarking the current `fork()` architecture
* Improved project documentation

### Planned

* Non-blocking sockets
* Linux `epoll` event loop
* Connection-oriented state machine
* Incremental non-blocking reads and writes
* Event-driven concurrency
* Benchmark comparison between `fork()` and `epoll`
* Logging and observability
* Additional robustness and stress testing

---

## Testing

The project uses Python-based integration tests to exercise the server through real TCP connections.

Tests cover areas including:

* HTTP status handling
* request parsing
* supported and unsupported methods
* malformed requests
* HTTP version validation
* persistent connections
* request/header limits
* filesystem behavior
* error responses
* timeout-related behavior

Run the complete test suite with:

```bash
make test
```

The test suite is treated as a regression suite: architectural changes should preserve the existing HTTP behavior unless a deliberate protocol/design change is being introduced.

---

## Development Roadmap

```text
[✓] TCP socket server
[✓] HTTP request parser
[✓] HTTP response generation
[✓] Static file serving
[✓] MIME detection
[✓] HTTP error handling
[✓] Secure document root
[✓] Persistent HTTP connections
[✓] Client timeouts
[✓] Fork-based concurrency
[✓] Automated tests

[ ] Concurrency stress tests
[ ] Fork architecture benchmark
[ ] Non-blocking sockets
[ ] epoll event loop
[ ] Connection state machine
[ ] Incremental non-blocking I/O
[ ] epoll architecture benchmark
[ ] Architecture comparison
[ ] Logging
[ ] Additional robustness testing
```

##

