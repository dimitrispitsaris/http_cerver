#!/usr/bin/env python3

import os
import shutil
import signal
import socket
import subprocess
import tempfile
import time
from pathlib import Path


HOST = "127.0.0.1"
SERVER = "./build/http_server"


def start_server(port, root):
    command = [
        SERVER,
        "--port",
        str(port),
        "--root",
        str(root),
        "--backlog",
        "32",
    ]

    process = subprocess.Popen(
        command,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        start_new_session=True,
    )

    deadline = time.monotonic() + 5.0

    while time.monotonic() < deadline:
        try:
            with socket.create_connection(
                (HOST, port),
                timeout=0.1,
            ):
                return process

        except OSError:
            time.sleep(0.05)

    stop_server(process)

    raise RuntimeError(
        f"Server failed to start on port {port}"
    )


def stop_server(process):
    if process.poll() is not None:
        return

    try:
        os.killpg(
            process.pid,
            signal.SIGTERM,
        )
        process.wait(timeout=2)

    except subprocess.TimeoutExpired:
        os.killpg(
            process.pid,
            signal.SIGKILL,
        )
        process.wait()

    except ProcessLookupError:
        pass


def send_request(port, target):
    request = (
        f"GET {target} HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Connection: close\r\n"
        "\r\n"
    ).encode("ascii")

    with socket.create_connection(
        (HOST, port),
        timeout=3,
    ) as sock:

        sock.sendall(request)

        response = bytearray()

        while True:
            try:
                chunk = sock.recv(4096)
            except ConnectionResetError:
                break

            if not chunk:
                break

            response.extend(chunk)

    return bytes(response)


def status_code(response):
    header_end = response.find(b"\r\n\r\n")

    if header_end == -1:
        raise AssertionError(
            "Response does not contain complete headers"
        )

    status_line = response[:header_end].split(
        b"\r\n",
        1,
    )[0]

    parts = status_line.split()

    if len(parts) < 2:
        raise AssertionError(
            f"Invalid status line: {status_line!r}"
        )

    return int(parts[1])


def create_test_environment():
    base = Path(
        tempfile.mkdtemp(
            prefix="http_cerver_security_"
        )
    )

    root = base / "root"
    outside = base / "outside"

    root.mkdir()
    outside.mkdir()

    return base, root, outside


def test_legitimate_file():
    base, root, outside = create_test_environment()

    port = 18101

    try:
        file_path = root / "inside.txt"

        file_path.write_text(
            "inside document root\n",
            encoding="utf-8",
        )

        server = start_server(port, root)

        try:
            response = send_request(
                port,
                "/inside.txt",
            )

            assert status_code(response) == 200
            assert b"inside document root" in response

        finally:
            stop_server(server)

    finally:
        shutil.rmtree(base)


def test_parent_traversal():
    base, root, outside = create_test_environment()

    port = 18102

    try:
        outside_file = outside / "secret.txt"

        outside_file.write_text(
            "secret outside document root\n",
            encoding="utf-8",
        )

        server = start_server(port, root)

        try:
            response = send_request(
                port,
                "/../outside/secret.txt",
            )

            assert status_code(response) == 403
            assert b"secret outside document root" not in response

        finally:
            stop_server(server)

    finally:
        shutil.rmtree(base)


def test_percent_encoded_parent_traversal():
    base, root, outside = create_test_environment()

    port = 18103

    try:
        outside_file = outside / "secret.txt"

        outside_file.write_text(
            "secret outside document root\n",
            encoding="utf-8",
        )

        server = start_server(port, root)

        try:
            response = send_request(
                port,
                "/%2e%2e/outside/secret.txt",
            )

            assert status_code(response) == 403
            assert b"secret outside document root" not in response

        finally:
            stop_server(server)

    finally:
        shutil.rmtree(base)


def test_uppercase_percent_encoded_parent_traversal():
    base, root, outside = create_test_environment()

    port = 18104

    try:
        outside_file = outside / "secret.txt"

        outside_file.write_text(
            "secret outside document root\n",
            encoding="utf-8",
        )

        server = start_server(port, root)

        try:
            response = send_request(
                port,
                "/%2E%2E/outside/secret.txt",
            )

            assert status_code(response) == 403
            assert b"secret outside document root" not in response

        finally:
            stop_server(server)

    finally:
        shutil.rmtree(base)

def test_nested_percent_encoded_parent_traversal():
    base, root, outside = create_test_environment()

    port = 18105

    try:
        nested = root / "nested"
        nested.mkdir()

        outside_file = outside / "secret.txt"

        outside_file.write_text(
            "secret outside document root\n",
            encoding="utf-8",
        )

        server = start_server(port, root)

        try:
            response = send_request(
                port,
                "/nested/%2e%2e/%2e%2e/outside/secret.txt",
            )

            code = status_code(response)

            assert code == 403

            assert (
                b"secret outside document root"
                not in response
            )

        finally:
            stop_server(server)

    finally:
        shutil.rmtree(base)


def test_symlink_to_outside_file():
    base, root, outside = create_test_environment()

    port = 18106

    try:
        outside_file = outside / "secret.txt"

        outside_file.write_text(
            "secret outside document root\n",
            encoding="utf-8",
        )

        symlink = root / "outside-link"
        symlink.symlink_to(outside_file)

        server = start_server(port, root)

        try:
            response = send_request(
                port,
                "/outside-link",
            )

            assert status_code(response) == 403
            assert b"secret outside document root" not in response

        finally:
            stop_server(server)

    finally:
        shutil.rmtree(base)


def test_symlinked_directory_to_outside():
    base, root, outside = create_test_environment()

    port = 18107

    try:
        outside_dir = outside / "private"
        outside_dir.mkdir()

        secret = outside_dir / "secret.txt"

        secret.write_text(
            "secret outside document root\n",
            encoding="utf-8",
        )

        symlink = root / "private-link"
        symlink.symlink_to(
            outside_dir,
            target_is_directory=True,
        )

        server = start_server(port, root)

        try:
            response = send_request(
                port,
                "/private-link/secret.txt",
            )

            assert status_code(response) == 403
            assert b"secret outside document root" not in response

        finally:
            stop_server(server)

    finally:
        shutil.rmtree(base)


def main():
    tests = [
        test_legitimate_file,
        test_parent_traversal,
        test_percent_encoded_parent_traversal,
        test_uppercase_percent_encoded_parent_traversal,
        test_nested_percent_encoded_parent_traversal,
        test_symlink_to_outside_file,
        test_symlinked_directory_to_outside,
    ]

    failures = 0

    for test in tests:
        try:
            test()
            print(f"PASS {test.__name__}")

        except Exception as exc:
            failures += 1
            print(f"FAIL {test.__name__}: {exc}")

    print()

    if failures:
        print(
            f"{failures} security test(s) failed"
        )
        return 1

    print(
        f"All {len(tests)} security tests passed"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
