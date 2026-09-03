#!/usr/bin/env python3

import subprocess


SERVER = "./build/http_server"


def run_server(*args):
    return subprocess.run(
        [SERVER, *args],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )


def assert_invalid(*args):
    result = run_server(*args)

    assert result.returncode != 0, (
        f"Expected failure for {args!r}, "
        f"got exit code {result.returncode}"
    )


def assert_invalid_message(expected, *args):
    result = run_server(*args)

    assert result.returncode != 0, (
        f"Expected failure for {args!r}, "
        f"got exit code {result.returncode}"
    )

    assert expected in result.stderr, (
        f"Expected {expected!r} in stderr, "
        f"got {result.stderr!r}"
    )


def test_port_zero():
    assert_invalid_message(
        "Invalid value for --port",
        "--port",
        "0",
    )


def test_port_negative():
    assert_invalid_message(
        "Invalid value for --port",
        "--port",
        "-1",
    )


def test_port_non_numeric():
    assert_invalid_message(
        "Invalid value for --port",
        "--port",
        "abc",
    )


def test_port_too_large():
    assert_invalid_message(
        "Port must be between 1 and 65535",
        "--port",
        "65536",
    )


def test_port_missing_value():
    assert_invalid_message(
        "Invalid value for --port",
        "--port",
    )


def test_backlog_zero():
    assert_invalid_message(
        "Invalid value for --backlog",
        "--backlog",
        "0",
    )


def test_backlog_non_numeric():
    assert_invalid_message(
        "Invalid value for --backlog",
        "--backlog",
        "abc",
    )


def test_backlog_missing_value():
    assert_invalid_message(
        "Invalid value for --backlog",
        "--backlog",
    )


def test_timeout_zero():
    assert_invalid_message(
        "Invalid value for --timeout",
        "--timeout",
        "0",
    )


def test_timeout_non_numeric():
    assert_invalid_message(
        "Invalid value for --timeout",
        "--timeout",
        "abc",
    )


def test_timeout_missing_value():
    assert_invalid_message(
        "Invalid value for --timeout",
        "--timeout",
    )


def test_root_missing_value():
    assert_invalid_message(
        "Missing value for --root",
        "--root",
    )


def test_http_buffer_zero():
    assert_invalid_message(
        "Invalid value for --http-buffer-size",
        "--http-buffer-size",
        "0",
    )


def test_max_headers_zero():
    assert_invalid_message(
        "Invalid value for --max-headers",
        "--max-headers",
        "0",
    )


def test_header_name_max_zero():
    assert_invalid_message(
        "Invalid value for --header-name-max",
        "--header-name-max",
        "0",
    )


def test_header_value_max_zero():
    assert_invalid_message(
        "Invalid value for --header-value-max",
        "--header-value-max",
        "0",
    )


def test_unknown_option():
    assert_invalid_message(
        "Unknown option: --does-not-exist",
        "--does-not-exist",
    )


def test_option_used_as_value():
    assert_invalid_message(
        "Invalid value for --port",
        "--port",
        "--backlog",
        "10",
    )


def main():
    tests = [
        test_port_zero,
        test_port_negative,
        test_port_non_numeric,
        test_port_too_large,
        test_port_missing_value,
        test_backlog_zero,
        test_backlog_non_numeric,
        test_backlog_missing_value,
        test_timeout_zero,
        test_timeout_non_numeric,
        test_timeout_missing_value,
        test_root_missing_value,
        test_http_buffer_zero,
        test_max_headers_zero,
        test_header_name_max_zero,
        test_header_value_max_zero,
        test_unknown_option,
        test_option_used_as_value,
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
            f"{failures} configuration test(s) failed"
        )
        return 1

    print(
        f"All {len(tests)} configuration tests passed"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
