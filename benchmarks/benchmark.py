#!/usr/bin/env python3

import argparse
import concurrent.futures
import os
import signal
import socket
import statistics
import subprocess
import sys
import time


HOST = "127.0.0.1"
SERVER = "./build/http_server"


def start_server(port: int,backlog:int) -> subprocess.Popen:
    """Start the HTTP server in its own process group."""
    process = subprocess.Popen(
        [
            SERVER,
            "--port",
            str(port),
            "--backlog",
            str(backlog),
        ],
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


def stop_server(process: subprocess.Popen) -> None:
    """Terminate the server and all forked children."""
    if process.poll() is not None:
        return

    try:
        os.killpg(process.pid, signal.SIGTERM)
        process.wait(timeout=2)

    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        process.wait()

    except ProcessLookupError:
        pass


def read_response(sock: socket.socket) -> bytes:
    """Read one complete HTTP response using Content-Length."""
    data = bytearray()

    while b"\r\n\r\n" not in data:
        chunk = sock.recv(4096)

        if not chunk:
            raise RuntimeError(
                "Connection closed before response headers"
            )

        data.extend(chunk)

        if len(data) > 1024 * 1024:
            raise RuntimeError(
                "Response headers exceed 1 MiB"
            )

    header_end = data.index(b"\r\n\r\n") + 4

    headers = bytes(data[:header_end])
    body = bytearray(data[header_end:])

    content_length = None

    for line in headers.split(b"\r\n")[1:]:
        if b":" not in line:
            continue

        name, value = line.split(b":", 1)

        if name.lower() == b"content-length":
            content_length = int(value.strip())
            break

    if content_length is None:
        raise RuntimeError(
            "Response has no Content-Length"
        )

    while len(body) < content_length:
        chunk = sock.recv(4096)

        if not chunk:
            raise RuntimeError(
                "Connection closed before response body"
            )

        body.extend(chunk)

    if len(body) != content_length:
        raise RuntimeError(
            "Received unexpected response body size"
        )

    return headers + bytes(body)


def request(port: int) -> float:
    """
    Perform one complete short-lived HTTP transaction:

        connect
        send request
        receive response
        close
    """
    request_data = (
        "GET /index.html HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Connection: close\r\n"
        "\r\n"
    ).encode("ascii")

    start = time.perf_counter()

    with socket.create_connection(
        (HOST, port),
        timeout=5,
    ) as sock:

        sock.sendall(request_data)

        response = read_response(sock)

    elapsed = time.perf_counter() - start

    if not response.startswith(b"HTTP/1.1 200"):
        raise RuntimeError(
            f"Unexpected response: {response[:64]!r}"
        )

    return elapsed


def run_once(
    port: int,
    connections: int,
) -> dict:
    """
    Run one benchmark iteration.

    Each concurrent worker creates exactly one TCP connection
    and performs exactly one HTTP request.
    """
    start = time.perf_counter()

    latencies = []

    with concurrent.futures.ThreadPoolExecutor(
        max_workers=connections
    ) as executor:

        futures = [
            executor.submit(request, port)
            for _ in range(connections)
        ]

        for future in concurrent.futures.as_completed(futures):
            latencies.append(future.result())

    elapsed = time.perf_counter() - start

    if len(latencies) != connections:
        raise RuntimeError(
            f"Completed {len(latencies)}/{connections} requests"
        )

    throughput = len(latencies) / elapsed

    return {
        "requests": len(latencies),
        "elapsed": elapsed,
        "throughput": throughput,
        "mean_ms": statistics.mean(latencies) * 1000,
        "p50_ms": percentile(latencies, 0.50) * 1000,
        "p95_ms": percentile(latencies, 0.95) * 1000,
        "p99_ms": percentile(latencies, 0.99) * 1000,
    }


def percentile(values: list[float], p: float) -> float:
    """Calculate a linearly interpolated percentile."""
    values = sorted(values)

    if not values:
        return 0.0

    index = (len(values) - 1) * p

    lower = int(index)
    upper = min(
        lower + 1,
        len(values) - 1,
    )

    fraction = index - lower

    return (
        values[lower]
        + (values[upper] - values[lower]) * fraction
    )


def benchmark(
    port: int,
    connections: int,
    warmup: int,
    runs: int,
    backlog: int,
) -> list[dict]:
    """Start the server and execute the benchmark."""
    server = start_server(port,backlog)

    try:
        print(
            f"Benchmarking {SERVER}\n"
            f"Connections: {connections}\n"
            f"Requests/connection: 1\n"
            f"Total requests/run: {connections}\n"
            f"Mode: short-lived\n"
            f"Warm-up runs: {warmup}\n"
            f"Measured runs: {runs}\n"
        )

        for i in range(warmup):
            print(
                f"Warm-up {i + 1}/{warmup}...",
                flush=True,
            )

            run_once(
                port,
                connections,
            )

        results = []

        for i in range(runs):
            print(
                f"Run {i + 1}/{runs}...",
                flush=True,
            )

            results.append(
                run_once(
                    port,
                    connections,
                )
            )

        return results

    finally:
        stop_server(server)


def summarize(results: list[dict]) -> dict:
    """Calculate median/min/max across measured runs."""
    metrics = [
        "throughput",
        "mean_ms",
        "p50_ms",
        "p95_ms",
        "p99_ms",
    ]

    summary = {}

    for metric in metrics:
        values = [result[metric] for result in results]

        summary[metric] = {
            "median": statistics.median(values),
            "min": min(values),
            "max": max(values),
        }

    return summary


def print_results(results: list[dict]) -> None:
    print()
    print("Measured runs")
    print("-" * 72)

    print(
        f"{'Run':>4} "
        f"{'Req/s':>12} "
        f"{'Mean ms':>12} "
        f"{'p50 ms':>12} "
        f"{'p95 ms':>12} "
        f"{'p99 ms':>12}"
    )

    for index, result in enumerate(results, start=1):
        print(
            f"{index:>4} "
            f"{result['throughput']:>12.2f} "
            f"{result['mean_ms']:>12.3f} "
            f"{result['p50_ms']:>12.3f} "
            f"{result['p95_ms']:>12.3f} "
            f"{result['p99_ms']:>12.3f}"
        )

    summary = summarize(results)

    print()
    print("Summary")
    print("-" * 72)

    for metric, label in [
        ("throughput", "Throughput"),
        ("mean_ms", "Mean latency"),
        ("p50_ms", "p50 latency"),
        ("p95_ms", "p95 latency"),
        ("p99_ms", "p99 latency"),
    ]:
        values = summary[metric]

        print(
            f"{label:<18}"
            f" median={values['median']:.3f}"
            f"  min={values['min']:.3f}"
            f"  max={values['max']:.3f}"
        )


def write_csv(
    filename: str,
    results: list[dict],
    connections: int,
) -> None:
    """Write raw benchmark runs to CSV."""
    import csv

    with open(
        filename,
        "w",
        newline="",
        encoding="utf-8",
    ) as file:

        writer = csv.writer(file)

        writer.writerow(
            [
                "connections",
                "requests_per_connection",
                "run",
                "requests",
                "elapsed_seconds",
                "throughput_req_s",
                "mean_ms",
                "p50_ms",
                "p95_ms",
                "p99_ms",
            ]
        )

        for index, result in enumerate(
            results,
            start=1,
        ):
            writer.writerow(
                [
                    connections,
                    1,
                    index,
                    result["requests"],
                    f"{result['elapsed']:.6f}",
                    f"{result['throughput']:.6f}",
                    f"{result['mean_ms']:.6f}",
                    f"{result['p50_ms']:.6f}",
                    f"{result['p95_ms']:.6f}",
                    f"{result['p99_ms']:.6f}",
                ]
            )


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Benchmark the fork-based C HTTP server "
            "using short-lived connections."
        )
    )

    parser.add_argument(
    "--backlog",
    type=int,
    default=1024,
    help="server listen backlog",
    )

    parser.add_argument(
        "-p",
        "--port",
        type=int,
        default=18090,
    )

    parser.add_argument(
        "-c",
        "--connections",
        type=int,
        default=10,
        help="number of concurrent connections",
    )

    parser.add_argument(
        "--warmup",
        type=int,
        default=2,
    )

    parser.add_argument(
        "--runs",
        type=int,
        default=5,
    )

    parser.add_argument(
        "--csv",
        help="write raw run results to CSV",
    )

    args = parser.parse_args()

    if args.connections <= 0:
        parser.error(
            "--connections must be positive"
        )

    if args.warmup < 0:
        parser.error(
            "--warmup cannot be negative"
        )

    if args.runs <= 0:
        parser.error(
            "--runs must be positive"
        )

    try:
        results = benchmark(
            args.port,
            args.connections,
            args.warmup,
            args.runs,
            args.backlog,
        )

    except (OSError, RuntimeError) as exc:
        print(
            f"Benchmark failed: {exc}",
            file=sys.stderr,
        )
        return 1

    print_results(results)

    if args.csv:
        write_csv(
            args.csv,
            results,
            args.connections,
        )

        print()
        print(f"CSV written to {args.csv}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
