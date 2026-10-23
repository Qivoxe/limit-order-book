from __future__ import annotations

import os
import subprocess
import sys
import unittest

from dashboard.benchmark_data import parse_benchmark_outputs
from dashboard.charts import cumulative_points, depth_figure
from dashboard.data_models import (
    Level,
    ProtocolError,
    Quote,
    Trade,
    TradeHistory,
    format_price_ticks,
    format_quantity,
    parse_frame,
)
from dashboard.engine_client import EngineClient, EngineProcessError
from dashboard.views import ladder_table

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ADAPTER = os.environ.get("LOB_PROTOCOL_EXECUTABLE", os.path.join(ROOT, "build-release", "lob_protocol"))


class ProtocolTests(unittest.TestCase):
    def test_parses_trade_events_and_snapshot(self) -> None:
        response = parse_frame([
            "BEGIN\t1\tOK\tSNAPSHOT",
            "QUOTE\t101\t105",
            "COUNT\t2",
            "LEVEL\tBID\t101\t4\t1",
            "LEVEL\tASK\t105\t3\t1",
            "END",
        ])
        self.assertEqual(response.quote, Quote(101, 105))
        self.assertEqual(response.order_count, 2)
        self.assertEqual(response.levels, (Level("BID", 101, 4, 1), Level("ASK", 105, 3, 1)))

        execution = parse_frame([
            "BEGIN\t1\tOK\tSUBMIT",
            "TRADE\t10\t20\t101\t2",
            "EVENT_TRADE\t10\t20\t101\t2",
            "EVENT_LEVEL\tASK\tUPDATED\t105\t3\t1",
            "EVENT_QUOTE\t101\t105",
            "END",
        ])
        self.assertEqual(execution.trades, (Trade(10, 20, 101, 2),))
        self.assertEqual([event.kind for event in execution.events],
                         ["EVENT_TRADE", "EVENT_LEVEL", "EVENT_QUOTE"])

    def test_rejects_incomplete_malformed_and_unsorted_frames(self) -> None:
        malformed_frames = [
            ["BEGIN\t1\tOK\tSNAPSHOT", "QUOTE\t-\t-"],
            ["BEGIN\t2\tOK\tSNAPSHOT", "QUOTE\t-\t-", "COUNT\t0", "END"],
            ["BEGIN\t1\tOK\tSNAPSHOT", "QUOTE\t-\t-", "COUNT\t0", "LEVEL\tBID\tnope\t1\t1", "END"],
            ["BEGIN\t1\tOK\tSNAPSHOT", "QUOTE\t-\t-", "COUNT\t0",
             "LEVEL\tBID\t99\t1\t1", "LEVEL\tBID\t100\t1\t1", "END"],
        ]
        for frame in malformed_frames:
            with self.subTest(frame=frame), self.assertRaises(ProtocolError):
                parse_frame(frame)

    def test_rejected_frame_requires_reason(self) -> None:
        with self.assertRaises(ProtocolError):
            parse_frame(["BEGIN\t1\tREJECTED\tSUBMIT", "END"])


class DashboardDataTests(unittest.TestCase):
    def test_integer_formatting_and_empty_book_rendering(self) -> None:
        self.assertEqual(format_price_ticks(12345), "12,345 ticks")
        self.assertEqual(format_price_ticks(None), "—")
        self.assertEqual(format_quantity(12345), "12,345")
        self.assertEqual(ladder_table((), "BID", 5, None),
                         '<div class="empty-book">No resting orders</div>')
        figure = depth_figure((), Quote(None, None), 5)
        self.assertEqual(len(figure.data), 0)

    def test_depth_chart_uses_sorted_cumulative_resting_quantities(self) -> None:
        levels = [Level("BID", 99, 2, 1), Level("BID", 100, 3, 2),
                  Level("ASK", 102, 4, 1), Level("ASK", 101, 5, 2)]
        self.assertEqual(cumulative_points(levels, "BID"), ([99, 100], [5, 3], [2, 3]))
        self.assertEqual(cumulative_points(levels, "ASK"), ([101, 102], [5, 9], [5, 4]))
        figure = depth_figure(levels, Quote(100, 101), 5)
        self.assertEqual([trace.name for trace in figure.data], ["Bids", "Asks"])
        self.assertEqual(list(figure.data[0].y), [5, 3])

    def test_trade_history_is_ordered_and_bounded(self) -> None:
        history = TradeHistory(limit=2)
        history.extend([Trade(1, 2, 100, 1), Trade(3, 4, 101, 2)])
        history.extend([Trade(5, 6, 102, 3)])
        self.assertEqual([row["sequence"] for row in history.rows], [2, 3])
        self.assertEqual([row["price"] for row in history.rows], [101, 102])

    def test_parses_actual_benchmark_output_format(self) -> None:
        run = parse_benchmark_outputs(
            "single-price submits       operations=200000 elapsed_s=0.010000 throughput_ops_s=20000000\n"
            "best/level queries         operations=1000000 elapsed_s=0.005000 throughput_ops_s=2.0e+08\n"
            "single submit latency_ns samples=50000 p50=60 p90=90 p95=100 p99=130 p99.9=200 sample_max=500\n",
            "Cancelled: 500000\nCancellation throughput: 15000000 cancellations/second\n"
            "Final book state: empty\n",
        )
        self.assertEqual(run.workloads[0]["unit"], "submissions/s")
        self.assertEqual(run.workloads[1]["unit"], "queries/s")
        self.assertEqual(run.latency["p99_9"], 200)
        self.assertEqual(run.cancellation["operations"], 500000)


class EngineClientTests(unittest.TestCase):
    def test_subprocess_failure_is_reported(self) -> None:
        with self.assertRaises(EngineProcessError):
            EngineClient(["/bin/false"], timeout=1)

    def test_malformed_subprocess_response_is_reported(self) -> None:
        program = 'import sys; print("READY\\t1", flush=True); sys.stdin.readline(); print("bad", flush=True)'
        client = EngineClient([sys.executable, "-c", program], timeout=1)
        try:
            with self.assertRaises(EngineProcessError):
                client.snapshot()
        finally:
            client.close()

    @unittest.skipUnless(os.environ.get("LOB_PROTOCOL_EXECUTABLE"),
                         "set LOB_PROTOCOL_EXECUTABLE for C++ adapter integration")
    def test_real_adapter_submit_duplicate_cancel_reset_and_bad_exit(self) -> None:
        client = EngineClient(os.environ["LOB_PROTOCOL_EXECUTABLE"])
        try:
            initial = client.snapshot()
            self.assertEqual(initial.order_count, 0)

            accepted = client.submit(100, "SELL", 105, 5)
            self.assertEqual(accepted.status, "OK")
            second_at_price = client.submit(103, "SELL", 105, 4)
            self.assertEqual(second_at_price.status, "OK")
            duplicate = client.submit(100, "SELL", 106, 1)
            self.assertEqual(duplicate.status, "REJECTED")

            crossing = client.submit(101, "BUY", 106, 7)
            self.assertEqual(crossing.trades, (Trade(101, 100, 105, 5), Trade(101, 103, 105, 2)))
            current = client.snapshot()
            self.assertEqual(current.order_count, 1)
            self.assertEqual(current.levels, (Level("ASK", 105, 2, 1),))

            malformed_exit = client._request("EXIT\tBAD")
            self.assertEqual(malformed_exit.status, "ERROR")
            self.assertEqual(client.snapshot().order_count, 1)

            invalid = client._request("SUBMIT\t102\tBUY\t0\t1")
            self.assertEqual(invalid.status, "REJECTED")
            cancelled = client.cancel(103)
            self.assertEqual(cancelled.status, "OK")
            self.assertEqual(client.snapshot().order_count, 0)

            reused = client.submit(103, "BUY", 99, 1)
            self.assertEqual(reused.status, "OK")
            reset = client.reset()
            self.assertEqual(reset.order_count, 0)
        finally:
            client.close()


class StreamlitChartRegressionTests(unittest.TestCase):
    def test_app_imports_when_launched_from_outside_repository_root(self) -> None:
        dashboard_dir = os.path.join(ROOT, "dashboard")
        import_command = (
            "import importlib.util, sys; "
            f"sys.path.insert(0, {dashboard_dir!r}); "
            f"spec=importlib.util.spec_from_file_location('cloud_app', {os.path.join(dashboard_dir, 'app.py')!r}); "
            "module=importlib.util.module_from_spec(spec); spec.loader.exec_module(module)"
        )
        result = subprocess.run([sys.executable, "-c", import_command], cwd="/tmp",
                                capture_output=True, text=True, check=False)
        self.assertEqual(result.returncode, 0, result.stderr)

    @unittest.skipUnless(os.path.isfile(ADAPTER), "build lob_protocol for the app integration test")
    def test_example_scenario_renders_nonempty_depth_chart(self) -> None:
        from streamlit.testing.v1 import AppTest

        app = AppTest.from_file(os.path.join(ROOT, "dashboard", "app.py"), default_timeout=20).run()
        next(button for button in app.button if button.label == "Load Example Scenario").click().run()
        next(button for button in app.button if button.label == "Run All").click().run()
        try:
            self.assertEqual(list(app.exception), [])
            self.assertGreater(len(app.get("plotly_chart")), 0)
        finally:
            client = app.session_state["engine_client"]
            if client is not None:
                client.close()


if __name__ == "__main__":
    unittest.main()
