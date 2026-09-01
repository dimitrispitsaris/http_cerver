#!/usr/bin/env python3

import socket
import sys


HOST = "127.0.0.1"
PORT = 8080


def send_request(request: str) -> bytes:
    with socket.create_connection((HOST, PORT), timeout=3) as sock:
        sock.sendall(request.encode("ascii"))

        response = bytearray()

        while True:
            chunk = sock.recv(4096)

            if not chunk:
                break

            response.extend(chunk)

    return bytes(response)

def receive_response(sock, pending=b""):
    data = bytearray(pending)

    # --------------------------------------------------------
    # Read until the complete HTTP header section arrives.
    # --------------------------------------------------------
    while b"\r\n\r\n" not in data:
        chunk = sock.recv(4096)

        if not chunk:
            raise AssertionError(
                "Connection closed before response headers"
            )

        data.extend(chunk)

    header_end = data.find(b"\r\n\r\n")
    body_start = header_end + 4

    headers = bytes(data[:header_end])

    content_length = header_value(
        headers.decode("iso-8859-1"),
        "Content-Length"
    )

    if content_length is None:
        raise AssertionError(
            "Response does not contain Content-Length"
        )

    try:
        body_length = int(content_length)
    except ValueError as exc:
        raise AssertionError(
            f"Invalid Content-Length: {content_length!r}"
        ) from exc

    total_length = body_start + body_length

    # --------------------------------------------------------
    # We might already have some or all of the body.
    # Keep reading until the complete response is present.
    # --------------------------------------------------------
    while len(data) < total_length:
        chunk = sock.recv(4096)

        if not chunk:
            raise AssertionError(
                "Connection closed before complete response"
            )

        data.extend(chunk)

    response = bytes(data[:total_length])
    remaining = bytes(data[total_length:])

    return response, remaining


def split_response(response: bytes):
    header_end = response.find(b"\r\n\r\n")

    if header_end == -1:
        raise AssertionError("Response does not contain header terminator")

    headers = response[:header_end].decode("iso-8859-1")
    body = response[header_end + 4:]

    return headers, body


def status_code(headers: str) -> int:
    status_line = headers.split("\r\n", 1)[0]
    parts = status_line.split()

    if len(parts) < 2:
        raise AssertionError(f"Invalid status line: {status_line!r}")

    return int(parts[1])


def header_value(headers: str, name: str):
    name = name.lower()

    for line in headers.split("\r\n")[1:]:
        if ":" not in line:
            continue

        key, value = line.split(":", 1)

        if key.strip().lower() == name:
            return value.strip()

    return None


def test_get_existing_file():
    request = (
        "GET /index.html HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Connection: close\r\n"
        "\r\n"
    )

    response = send_request(request)

    headers, body = split_response(response)

    assert status_code(headers) == 200
    assert header_value(headers, "Content-Type") == "text/html"
    assert int(header_value(headers, "Content-Length")) == len(body)
    assert b"<html" in body


def test_head_existing_file():
    request = (
        "HEAD /index.html HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Connection: close\r\n"
        "\r\n"
    )

    response = send_request(request)

    headers, body = split_response(response)

    assert status_code(headers) == 200
    assert int(header_value(headers, "Content-Length")) > 0
    assert body == b""


def test_get_missing_file():
    request = (
        "GET /does-not-exist.html HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Connection: close\r\n"
        "\r\n"
    )

    response = send_request(request)

    headers, body = split_response(response)

    assert status_code(headers) == 404
    assert b"404 Not Found" in body


def test_unsupported_method():
    request = (
        "POST /index.html HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Connection: close\r\n"
        "\r\n"
    )

    response = send_request(request)

    headers, body = split_response(response)

    assert status_code(headers) == 405
    assert b"405 Method Not Allowed" in body


def test_http11_missing_host():
    request = (
        "GET /index.html HTTP/1.1\r\n"
        "Connection: close\r\n"
        "\r\n"
    )

    response = send_request(request)

    headers, body = split_response(response)

    assert status_code(headers) == 400
    assert b"400 Bad Request" in body


def test_root_index():
    request = (
        "GET / HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Connection: close\r\n"
        "\r\n"
    )

    response = send_request(request)

    headers, body = split_response(response)

    assert status_code(headers) == 200
    assert b"<html" in body


def test_connection_close():
    request = (
        "GET /index.html HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Connection: close\r\n"
        "\r\n"
    )

    with socket.create_connection((HOST, PORT), timeout=3) as sock:
        sock.sendall(request.encode("ascii"))

        response = bytearray()

        while True:
            chunk = sock.recv(4096)

            if not chunk:
                break

            response.extend(chunk)

        assert response

        headers, body = split_response(bytes(response))

        assert status_code(headers) == 200
        assert header_value(headers, "Connection") == "close"

        # The server should close the TCP connection.
        assert sock.recv(1) == b""


def test_keep_alive():
    with socket.create_connection((HOST, PORT), timeout=3) as sock:

        request1 = (
            "GET /index.html HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "\r\n"
        )

        sock.sendall(request1.encode("ascii"))

        response1, pending = receive_response(sock)

        headers1, body1 = split_response(response1)

        assert status_code(headers1) == 200
        assert header_value(headers1, "Connection") == "keep-alive"
        assert len(body1) == int(
            header_value(headers1, "Content-Length")
        )

        request2 = (
            "GET / HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Connection: close\r\n"
            "\r\n"
        )

        sock.sendall(request2.encode("ascii"))

        response2, pending = receive_response(
            sock,
            pending
        )

        headers2, body2 = split_response(response2)

        assert status_code(headers2) == 200
        assert len(body2) == int(
            header_value(headers2, "Content-Length")
        )
        assert b"<html" in body2

        assert pending == b""

TESTS = [
    test_get_existing_file,
    test_head_existing_file,
    test_get_missing_file,
    test_unsupported_method,
    test_http11_missing_host,
    test_root_index,
    test_connection_close,
    test_keep_alive,
]


def main():
    failures = 0

    for test in TESTS:
        try:
            test()
            print(f"PASS {test.__name__}")

        except Exception as exc:
            failures += 1
            print(f"FAIL {test.__name__}: {exc}")

    print()

    if failures:
        print(f"{failures} test(s) failed")
        return 1

    print(f"All {len(TESTS)} tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
