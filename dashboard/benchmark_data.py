from __future__ import annotations

import re
from dataclasses import dataclass


_WORKLOAD = re.compile(
    r"^(?P<name>.+?)\s+operations=(?P<operations>\d+) "
    r"elapsed_s=(?P<seconds>[0-9.eE+-]+) throughput_ops_s=(?P<rate>[0-9.eE+-]+)$"
)
_LATENCY = re.compile(
    r"^single submit latency_ns samples=(\d+) p50=(\d+) p90=(\d+) "
    r"p95=(\d+) p99=(\d+)(?: p99\.9=(\d+))? sample_max=(\d+)$"
)
_CANCELLED = re.compile(r"^Cancelled: (\d+)$", re.MULTILINE)
_CANCEL_RATE = re.compile(r"^Cancellation throughput: ([0-9.eE+-]+) cancellations/second$", re.MULTILINE)


@dataclass(frozen=True)
class BenchmarkRun:
    workloads: tuple[dict[str, int | float | str], ...]
    latency: dict[str, int] | None
    cancellation: dict[str, int | float]


def parse_benchmark_outputs(workload_output: str, cancellation_output: str) -> BenchmarkRun:
    workloads: list[dict[str, int | float | str]] = []
    latency: dict[str, int] | None = None
    for line in workload_output.splitlines():
        candidate = line.strip()
        row = _WORKLOAD.match(candidate)
        if row:
            name = row.group("name").strip()
            operations = int(row.group("operations"))
            if "cancellations" in name:
                unit = "cancellations/s"
            elif "queries" in name:
                unit = "queries/s"
            elif "submits/cancels" in name or "multi-order" in name:
                unit = "API calls/s"
            else:
                unit = "submissions/s"
            workloads.append({"name": name, "operations": operations,
                              "elapsed_s": float(row.group("seconds")),
                              "throughput": float(row.group("rate")), "unit": unit})
            continue
        latency_match = _LATENCY.match(candidate)
        if latency_match:
            values = [int(value) for value in latency_match.groups() if value is not None]
            latency = dict(zip(("samples", "p50", "p90", "p95", "p99", "p99_9", "sample_max"),
                               values))
    cancelled = _CANCELLED.search(cancellation_output)
    cancel_rate = _CANCEL_RATE.search(cancellation_output)
    if not workloads or latency is None or cancelled is None or cancel_rate is None:
        raise ValueError("Benchmark output is incomplete or does not match the documented format")
    if "Final book state: empty" not in cancellation_output:
        raise ValueError("Cancellation benchmark did not verify an empty final book")
    return BenchmarkRun(tuple(workloads), latency,
                        {"operations": int(cancelled.group(1)),
                         "throughput": float(cancel_rate.group(1)),
                         "unit": "cancellations/s"})
