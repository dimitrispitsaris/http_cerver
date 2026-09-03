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

    if body_length < 0:
        raise AssertionError(
            f"Negative Content-Length: {body_length}"
        )

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
        raise AssertionError(
            "Response does not contain header terminator"
        )

    headers = response[:header_end].decode("iso-8859-1")
    body = response[header_end + 4:]

    return headers, body


def status_code(headers: str) -> int:
    status_line = headers.split("\r\n", 1)[0]
    parts = status_line.split()

    if len(parts) < 2:
        raise AssertionError(
            f"Invalid status line: {status_line!r}"
        )

    try:
        return int(parts[1])
    except ValueError as exc:
        raise AssertionError(
            f"Invalid status code in status line: {status_line!r}"
        ) from exc


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


def test_malformed_request_line():
    request = (
        "GET /index.html\r\n"
        "Host: localhost\r\n"
        "Connection: close\r\n"
        "\r\n"
    )

    response = send_request(request)

    headers, body = split_response(response)

    assert status_code(headers) == 400
    assert b"400 Bad Request" in body


def test_extra_request_line_field():
    request = (
        "GET /index.html HTTP/1.1 EXTRA\r\n"
        "Host: localhost\r\n"
        "Connection: close\r\n"
        "\r\n"
    )

    response = send_request(request)

    headers, body = split_response(response)

    assert status_code(headers) == 400
    assert b"400 Bad Request" in body


def test_missing_request_target():
    request = (
        "GET HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Connection: close\r\n"
        "\r\n"
    )

    response = send_request(request)

    headers, body = split_response(response)

    assert status_code(headers) == 400
    assert b"400 Bad Request" in body


def test_unsupported_http_version():
    request = (
        "GET /index.html HTTP/2.0\r\n"
        "Host: localhost\r\n"
        "Connection: close\r\n"
        "\r\n"
    )

    response = send_request(request)

    headers, body = split_response(response)

    assert status_code(headers) == 400
    assert b"400 Bad Request" in body


def test_duplicate_host():
    request = (
        "GET /index.html HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Host: example.com\r\n"
        "Connection: close\r\n"
        "\r\n"
    )

    response = send_request(request)

    headers, body = split_response(response)

    assert status_code(headers) == 400
    assert b"400 Bad Request" in body


def test_http10_default_close():
    request = (
        "GET /index.html HTTP/1.0\r\n"
        "Host: localhost\r\n"
        "\r\n"
    )

    response = send_request(request)

    headers, body = split_response(response)

    assert status_code(headers) == 200
    assert header_value(headers, "Connection") == "close"
    assert len(body) == int(
        header_value(headers, "Content-Length")
    )


def test_http10_connection_close():
    request = (
        "GET /index.html HTTP/1.0\r\n"
        "Host: localhost\r\n"
        "Connection: close\r\n"
        "\r\n"
    )

    response = send_request(request)

    headers, body = split_response(response)

    assert status_code(headers) == 200
    assert header_value(headers, "Connection") == "close"
    assert len(body) == int(
        header_value(headers, "Content-Length")
    )


def test_http11_default_keep_alive():
    with socket.create_connection(
        (HOST, PORT),
        timeout=3
    ) as sock:

        request = (
            "GET /index.html HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "\r\n"
        )

        sock.sendall(request.encode("ascii"))

        response, pending = receive_response(sock)

        headers, body = split_response(response)

        assert status_code(headers) == 200
        assert header_value(headers, "Connection") == "keep-alive"
        assert len(body) == int(
            header_value(headers, "Content-Length")
        )
        assert body

        request = (
            "GET / HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Connection: close\r\n"
            "\r\n"
        )

        sock.sendall(request.encode("ascii"))

        response, pending = receive_response(
            sock,
            pending
        )

        headers, body = split_response(response)

        assert status_code(headers) == 200
        assert header_value(headers, "Connection") == "close"
        assert body

        assert pending == b""


def test_http11_connection_keep_alive():
    with socket.create_connection(
        (HOST, PORT),
        timeout=3
    ) as sock:

        request = (
            "GET /index.html HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Connection: keep-alive\r\n"
            "\r\n"
        )

        sock.sendall(request.encode("ascii"))

        response, pending = receive_response(sock)

        headers, body = split_response(response)

        assert status_code(headers) == 200
        assert header_value(headers, "Connection") == "keep-alive"
        assert body

        request = (
            "GET / HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Connection: close\r\n"
            "\r\n"
        )

        sock.sendall(request.encode("ascii"))

        response, pending = receive_response(
            sock,
            pending
        )

        headers, body = split_response(response)

        assert status_code(headers) == 200
        assert header_value(headers, "Connection") == "close"
        assert body

        assert pending == b""


def test_http11_connection_close():
    request = (
        "GET /index.html HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Connection: close\r\n"
        "\r\n"
    )

    response = send_request(request)

    headers, body = split_response(response)

    assert status_code(headers) == 200
    assert header_value(headers, "Connection") == "close"
    assert body


def test_concurrent_clients():
    """
    Verify that a client with an incomplete request does not block
    another client from being served.

    The fork-based server should handle the two connections in
    independent child processes.
    """

    slow_request = (
        "GET /index.html HTTP/1.1\r\n"
        "Host: localhost\r\n"
    )

    fast_request = (
        "GET /index.html HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Connection: close\r\n"
        "\r\n"
    )

    with socket.create_connection(
        (HOST, PORT),
        timeout=3
    ) as slow_sock:

        # Deliberately omit the final CRLFCRLF so the child handling
        # this connection remains blocked waiting for request completion.
        slow_sock.sendall(slow_request.encode("ascii"))

        with socket.create_connection(
            (HOST, PORT),
            timeout=2
        ) as fast_sock:

            fast_sock.sendall(fast_request.encode("ascii"))

            response = bytearray()

            while True:
                chunk = fast_sock.recv(4096)

                if not chunk:
                    break

                response.extend(chunk)

            assert response

            headers, body = split_response(bytes(response))

            assert status_code(headers) == 200
            assert header_value(headers, "Connection") == "close"
            assert len(body) == int(
                header_value(headers, "Content-Length")
            )


def test_path_traversal():
    request = (
        "GET /../README.md HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Connection: close\r\n"
        "\r\n"
    )

    response = send_request(request)

    headers, body = split_response(response)

    assert status_code(headers) == 403


def test_encoded_path_traversal():
    request = (
        "GET /%2e%2e/README.md HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Connection: close\r\n"
        "\r\n"
    )

    response = send_request(request)

    headers, body = split_response(response)

    assert status_code(headers) == 403


def test_uppercase_encoded_path_traversal():
    request = (
        "GET /%2E%2E/README.md HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Connection: close\r\n"
        "\r\n"
    )

    response = send_request(request)

    headers, body = split_response(response)

    assert status_code(headers) == 403


def test_fragmented_request():
    with socket.create_connection(
        (HOST, PORT),
        timeout=3
    ) as sock:

        parts = [
            "GET /index.html HTTP/1.1\r\n",
            "Host: local",
            "host\r\nConnection: ",
            "close\r\n\r\n",
        ]

        for part in parts:
            sock.sendall(part.encode("ascii"))

        response = bytearray()

        while True:
            chunk = sock.recv(4096)

            if not chunk:
                break

            response.extend(chunk)

        headers, body = split_response(bytes(response))

        assert status_code(headers) == 200
        assert len(body) == int(
            header_value(headers, "Content-Length")
        )


def test_pipelined_requests():
    with socket.create_connection(
        (HOST, PORT),
        timeout=3
    ) as sock:

        request = (
            "GET /index.html HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "\r\n"
            "GET / HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Connection: close\r\n"
            "\r\n"
        )

        sock.sendall(request.encode("ascii"))

        response1, pending = receive_response(sock)

        headers1, body1 = split_response(response1)

        assert status_code(headers1) == 200
        assert body1

        response2, pending = receive_response(
            sock,
            pending
        )

        headers2, body2 = split_response(response2)

        assert status_code(headers2) == 200
        assert body2
        assert pending == b""

def test_content_length_zero():
    request = (
        "GET /index.html HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n"
        "\r\n"
    )

    response = send_request(request)

    headers, body = split_response(response)

    assert status_code(headers) == 200
    assert header_value(headers, "Connection") == "close"


def test_get_request_body_rejected():
    request = (
        "GET /index.html HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: 5\r\n"
        "Connection: close\r\n"
        "\r\n"
        "abcde"
    )

    response = send_request(request)

    headers, body = split_response(response)

    assert status_code(headers) == 400
    assert header_value(headers, "Connection") == "close"
    assert b"Request bodies are not supported" in body

def test_head_request_body_rejected():
    request = (
        "HEAD /index.html HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: 5\r\n"
        "Connection: close\r\n"
        "\r\n"
        "abcde"
    )

    response = send_request(request)

    headers, body = split_response(response)

    assert status_code(headers) == 400
    assert header_value(headers, "Connection") == "close"
    assert b"Request bodies are not supported" in body

def test_transfer_encoding_rejected():
    request = (
        "GET /index.html HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Transfer-Encoding: chunked\r\n"
        "Connection: close\r\n"
        "\r\n"
    )

    response = send_request(request)

    headers, body = split_response(response)

    assert status_code(headers) == 400
    assert header_value(headers, "Connection") == "close"
    assert b"Transfer-Encoding is not supported" in body

def test_duplicate_content_length_rejected():
    request = (
        "GET /index.html HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: 0\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n"
        "\r\n"
    )

    response = send_request(request)

    headers, body = split_response(response)

    assert status_code(headers) == 400
    assert header_value(headers, "Connection") == "close"
    assert b"Duplicate Content-Length" in body


def test_rejected_body_closes_connection():
    with socket.create_connection(
        (HOST, PORT),
        timeout=3,
    ) as sock:

        bad_request = (
            "GET /index.html HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Content-Length: 5\r\n"
            "Connection: keep-alive\r\n"
            "\r\n"
            "abcde"
        ).encode("ascii")

        sock.sendall(bad_request)

        response = bytearray()

        while True:
            chunk = sock.recv(4096)

            if not chunk:
                break

            response.extend(chunk)

        headers, body = split_response(
            bytes(response)
        )

        assert status_code(headers) == 400
        assert header_value(headers, "Connection") == "close"
        assert b"Request bodies are not supported" in body

TESTS = [
    test_get_existing_file,
    test_head_existing_file,
    test_get_missing_file,
    test_unsupported_method,
    test_http11_missing_host,
    test_root_index,
    test_connection_close,
    test_keep_alive,
    test_malformed_request_line,
    test_extra_request_line_field,
    test_missing_request_target,
    test_unsupported_http_version,
    test_duplicate_host,
    test_http10_default_close,
    test_http10_connection_close,
    test_http11_default_keep_alive,
    test_http11_connection_keep_alive,
    test_http11_connection_close,
    test_concurrent_clients,
    test_path_traversal,
    test_encoded_path_traversal,
    test_uppercase_encoded_path_traversal,
    test_fragmented_request,
    test_pipelined_requests,
    test_content_length_zero,
    test_get_request_body_rejected,
    test_head_request_body_rejected,
    test_transfer_encoding_rejected,
    test_duplicate_content_length_rejected,
    test_rejected_body_closes_connection,

]


def main():
    failures = 0

    print(f"Running {len(TESTS)} tests...\n")

    for test in TESTS:
        try:
            test()
            print(f"PASS {test.__name__}")

        except Exception as exc:
            failures += 1
            print(
                f"FAIL {test.__name__}: "
                f"{type(exc).__name__}: {exc}"
            )

    print()

    if failures:
        print(f"{failures} test(s) failed")
        return 1

    print(f"All {len(TESTS)} tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())

