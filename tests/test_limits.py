#!/usr/bin/env python3

import os
import signal
import socket
import subprocess
import time


HOST = "127.0.0.1"
SERVER = "./build/http_server"


def start_server(port, *args):
    command = [
        SERVER,
        "--port",
        str(port),
        *args,
    ]

    process = subprocess.Popen(
        command,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        start_new_session=True,
    )

    # Wait until the server is accepting connections.
    for _ in range(50):
        try:
            with socket.create_connection(
                (HOST, port),
                timeout=0.1,
            ):
                return process

        except OSError:
            time.sleep(0.1)

    # Server failed to start.
    process.kill()
    process.wait()

    raise RuntimeError(
        f"Server failed to start on port {port}"
    )


def stop_server(process):
    if process.poll() is not None:
        return

    # Because start_new_session=True was used,
    # process.pid is also the process-group ID.
    os.killpg(
        process.pid,
        signal.SIGTERM
    )

    try:
        process.wait(timeout=2)

    except subprocess.TimeoutExpired:
        os.killpg(
            process.pid,
            signal.SIGKILL
        )

        process.wait()


def send_request(port, request):
    with socket.create_connection(
        (HOST, port),
        timeout=3,
    ) as sock:

        sock.sendall(
            request.encode("ascii")
        )

        response = bytearray()

        while True:
            try:
                chunk = sock.recv(4096)

            except ConnectionResetError:
                # The server may reset the connection after
                # sending the HTTP response when unread request
                # bytes remain in the receive queue.
                break

            if not chunk:
                break

            response.extend(chunk)

    return bytes(response)



def get_status(response):
    header_end = response.find(
        b"\r\n\r\n"
    )

    if header_end == -1:
        raise AssertionError(
            "Response does not contain complete headers"
        )

    status_line = response[
        :header_end
    ].split(
        b"\r\n",
        1
    )[0]

    parts = status_line.split()

    if len(parts) < 2:
        raise AssertionError(
            f"Invalid status line: {status_line!r}"
        )

    try:
        return int(parts[1])

    except ValueError as exc:
        raise AssertionError(
            f"Invalid status code: {parts[1]!r}"
        ) from exc


def test_max_headers():
    """
    Configure a maximum of 2 headers.

    Host
    Connection
    X-Third

    The third header must be rejected.
    """
    port = 18080

    server = start_server(
        port,
        "--max-headers",
        "2",
    )

    try:
        request = (
            "GET /index.html HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Connection: close\r\n"
            "X-Third: value\r\n"
            "\r\n"
        )

        response = send_request(
            port,
            request
        )

        assert get_status(response) == 400

    finally:
        stop_server(server)


def test_header_name_limit():
    """
    Configure a maximum header-name length of 10.

    The current C implementation uses >= for the
    limit check, so a 10-byte header name is rejected.
    """
    port = 18081

    server = start_server(
        port,
        "--header-name-max",
        "10",
    )

    try:
        request = (
            "GET /index.html HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "1234567890: value\r\n"
            "Connection: close\r\n"
            "\r\n"
        )

        response = send_request(
            port,
            request
        )

        assert get_status(response) == 400

    finally:
        stop_server(server)


def test_header_value_limit():
    """
    Configure a maximum header-value length of 10.

    The current C implementation uses >= for the
    limit check, so a 10-byte value is rejected.
    """
    port = 18082

    server = start_server(
        port,
        "--header-value-max",
        "10",
    )

    try:
        request = (
            "GET /index.html HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "X-Test: 1234567890\r\n"
            "Connection: close\r\n"
            "\r\n"
        )

        response = send_request(
            port,
            request
        )

        assert get_status(response) == 400

    finally:
        stop_server(server)


def test_http_buffer_limit():
    """
    Configure a very small HTTP receive buffer.

    The complete request is intentionally larger than
    64 bytes, so the server must reject it.
    """
    port = 18083

    server = start_server(
        port,
        "--http-buffer-size",
        "64",
    )

    try:
        request = (
            "GET /index.html HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "X-Padding: 12345678901234567890\r\n"
            "Connection: close\r\n"
            "\r\n"
        )

        assert len(
            request.encode("ascii")
        ) > 64

        response = send_request(
            port,
            request
        )

        assert get_status(response) == 400

    finally:
        stop_server(server)


TESTS = [
    test_max_headers,
    test_header_name_limit,
    test_header_value_limit,
    test_http_buffer_limit,
]


def main():
    failures = 0

    for test in TESTS:
        try:
            test()
            print(
                f"PASS {test.__name__}"
            )

        except Exception as exc:
            failures += 1

            print(
                f"FAIL {test.__name__}: {exc}"
            )

    print()

    if failures:
        print(
            f"{failures} test(s) failed"
        )

        return 1

    print(
        f"All {len(TESTS)} limit tests passed"
    )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())



