from __future__ import annotations

import os
import platform
import re
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

import plotly.express as px
import plotly.graph_objects as go
import streamlit as st

from dashboard.benchmark_data import BenchmarkRun, parse_benchmark_outputs
from dashboard.charts import depth_figure
from dashboard.data_models import EngineResponse, Event, Level, Quote, TradeHistory, format_price_ticks, format_quantity
from dashboard.engine_client import EngineClient, EngineProcessError
from dashboard.styles import CSS
from dashboard.views import ladder_table


PROTOCOL_EXE = Path(os.environ.get("LOB_PROTOCOL_EXECUTABLE", ROOT / "build-release" / "lob_protocol"))
BENCHMARK_EXE = ROOT / "build-release" / "lob_benchmark"
CANCELLATION_EXE = ROOT / "build-release" / "cancellation_benchmark"
MAX_HISTORY = 200

SCENARIO = [
    ("submit", 101, "SELL", 105, 5, "Resting ask at 105, quantity 5"),
    ("submit", 102, "SELL", 105, 4, "Second ask joins the same price behind order 101"),
    ("submit", 201, "BUY", 100, 6, "Non-crossing bid at 100, quantity 6"),
    ("submit", 202, "BUY", 105, 3, "Partial fill: buys 3 from the first ask"),
    ("submit", 203, "BUY", 105, 8, "Completes order 101, then fills order 102 and rests the remainder"),
    ("cancel", 201, "", 0, 0, "Cancel the remaining bid at 100"),
]


def initialize_state() -> None:
    initial = {
        "engine_client": None,
        "engine_error": None,
        "last_command": "None",
        "flash": None,
        "trade_history": TradeHistory(limit=MAX_HISTORY),
        "trade_count": 0,
        "event_rows": [],
        "event_sequence": 1,
        "action_rows": [],
        "scenario_actions": [],
        "scenario_index": 0,
        "scenario_mode": False,
        "benchmark_runs": [],
        "benchmark_error": None,
    }
    for key, value in initial.items():
        if key not in st.session_state:
            st.session_state[key] = value


def active_client() -> EngineClient | None:
    client = st.session_state.engine_client
    return client if client is not None and client.alive else None


def set_flash(kind: str, message: str) -> None:
    st.session_state.flash = (kind, message)


def record_response(response: EngineResponse, command: str) -> None:
    st.session_state.last_command = command
    if response.status == "ERROR":
        raise EngineProcessError(response.reason or "Engine reported an internal error")
    if response.trades:
        history = st.session_state.trade_history
        history.extend(response.trades)
        st.session_state.trade_count += len(response.trades)
    for event in response.events:
        fields = ", ".join(str(value) for value in event.fields)
        st.session_state.event_rows.append({
            "sequence": st.session_state.event_sequence,
            "event": event.kind.removeprefix("EVENT_"),
            "details": fields,
        })
        st.session_state.event_sequence += 1
    st.session_state.event_rows = st.session_state.event_rows[-MAX_HISTORY:]
    st.session_state.action_rows.append({"action": command, "status": response.status,
                                         "detail": response.reason or ""})
    st.session_state.action_rows = st.session_state.action_rows[-MAX_HISTORY:]


def start_or_reset_engine() -> None:
    client = st.session_state.engine_client
    try:
        if client is not None and client.alive:
            response = client.reset()
        else:
            if not PROTOCOL_EXE.is_file():
                raise EngineProcessError(f"Engine adapter not found: {PROTOCOL_EXE}. Build the Release target first.")
            client = EngineClient(PROTOCOL_EXE)
            st.session_state.engine_client = client
            response = client.reset()
        record_response(response, "RESET")
        st.session_state.engine_error = None
        st.session_state.trade_history = TradeHistory(limit=MAX_HISTORY)
        st.session_state.trade_count = 0
        st.session_state.event_rows = []
        st.session_state.event_sequence = 1
        st.session_state.action_rows = []
        st.session_state.scenario_actions = []
        st.session_state.scenario_index = 0
        st.session_state.scenario_mode = False
        st.session_state.benchmark_runs = []
        st.session_state.benchmark_error = None
        set_flash("success", "Engine reset. The book is empty and ready for orders.")
    except (EngineProcessError, OSError) as error:
        st.session_state.engine_error = str(error)
        set_flash("error", str(error))


def ensure_engine() -> EngineClient:
    client = active_client()
    if client is None:
        raise EngineProcessError("Start or reset the engine before submitting commands.")
    return client


def integer_input(value: str, label: str, minimum: int = 1) -> int:
    if not re.fullmatch(r"[0-9]+", value.strip()):
        raise ValueError(f"{label} must be a whole-number integer.")
    number = int(value)
    if number < minimum:
        raise ValueError(f"{label} must be at least {minimum}.")
    return number


def apply_scenario_action(action: tuple[str, int, str, int, int, str]) -> EngineResponse:
    kind, order_id, side, price, quantity, description = action
    client = ensure_engine()
    if kind == "submit":
        response = client.submit(order_id, side, price, quantity)
        command = f"SCENARIO SUBMIT {order_id} {side} {price} {quantity}"
    else:
        response = client.cancel(order_id)
        command = f"SCENARIO CANCEL {order_id}"
    record_response(response, command)
    set_flash("success" if response.status == "OK" else "error",
              f"Scenario: {description} — {response.status.lower()}"
              + (f" ({response.reason})" if response.reason else ""))
    return response


def snapshot_now() -> EngineResponse | None:
    client = active_client()
    if client is None:
        return None
    try:
        response = client.snapshot()
        if response.status != "OK":
            raise EngineProcessError(response.reason or "Snapshot request failed")
        return response
    except EngineProcessError as error:
        st.session_state.engine_error = str(error)
        return None


def run_benchmarks() -> None:
    if not BENCHMARK_EXE.is_file() or not CANCELLATION_EXE.is_file():
        st.session_state.benchmark_error = (
            "Release benchmarks are missing. Build them with "
            "`cmake --build build-release --target lob_benchmark cancellation_benchmark`."
        )
        return
    try:
        workload = subprocess.run([str(BENCHMARK_EXE)], cwd=ROOT, capture_output=True,
                                  text=True, errors="replace", timeout=120, check=False)
        cancellation = subprocess.run([str(CANCELLATION_EXE)], cwd=ROOT, capture_output=True,
                                      text=True, errors="replace", timeout=120, check=False)
        if workload.returncode != 0:
            raise RuntimeError(workload.stderr.strip() or f"Benchmark exited {workload.returncode}")
        if cancellation.returncode != 0:
            raise RuntimeError(cancellation.stderr.strip() or f"Cancellation benchmark exited {cancellation.returncode}")
        parsed = parse_benchmark_outputs(workload.stdout, cancellation.stdout)
        st.session_state.benchmark_runs.append({
            "run": parsed,
            "recorded_at": datetime.now(timezone.utc).isoformat(timespec="seconds"),
            "platform": platform.platform(),
            "python": sys.version.split()[0],
        })
        st.session_state.benchmark_runs = st.session_state.benchmark_runs[-10:]
        st.session_state.benchmark_error = None
    except (OSError, subprocess.TimeoutExpired, RuntimeError, ValueError) as error:
        st.session_state.benchmark_error = str(error)


def render_header() -> None:
    client = st.session_state.engine_client
    if st.session_state.engine_error:
        status, status_class = "ERROR", "terminal-error"
    elif client is not None and client.alive:
        status, status_class = "RUNNING", "terminal-running"
    else:
        status, status_class = "STOPPED", "terminal-stopped"
    st.markdown(
        '<div class="terminal-header"><span class="terminal-title">LOB TERMINAL</span><br>'
        '<span class="terminal-subtitle">C++ LIMIT ORDER BOOK &amp; MATCHING ENGINE</span>'
        f'<span style="float:right" class="{status_class}">ENGINE {status} &nbsp;·&nbsp; '
        f'{"DEMO SCENARIO" if st.session_state.scenario_mode else "MANUAL SIMULATION"}</span></div>',
        unsafe_allow_html=True,
    )


def render_metrics(snapshot: EngineResponse) -> None:
    assert snapshot.quote is not None
    bids = [level for level in snapshot.levels if level.side == "BID"]
    asks = [level for level in snapshot.levels if level.side == "ASK"]
    best_bid, best_ask = snapshot.quote.bid, snapshot.quote.ask
    spread = best_ask - best_bid if best_bid is not None and best_ask is not None else None
    metrics = [
        ("Best bid", format_price_ticks(best_bid)),
        ("Best ask", format_price_ticks(best_ask)),
        ("Spread", format_price_ticks(spread)),
        ("Resting orders", format_quantity(snapshot.order_count)),
        ("Total bid qty", format_quantity(sum(level.quantity for level in bids))),
        ("Total ask qty", format_quantity(sum(level.quantity for level in asks))),
        ("Session trades", format_quantity(st.session_state.trade_count)),
    ]
    columns = st.columns(4)
    for index, (label, value) in enumerate(metrics):
        columns[index % 4].metric(label, value)


def render_ladder(levels: tuple[Level, ...], quote: Quote, depth: int) -> None:
    bids, asks = st.columns(2)
    with bids:
        st.markdown("#### Bids")
        st.markdown(ladder_table(levels, "BID", depth, quote.bid), unsafe_allow_html=True)
    with asks:
        st.markdown("#### Asks")
        st.markdown(ladder_table(levels, "ASK", depth, quote.ask), unsafe_allow_html=True)


def render_benchmarks() -> None:
    st.subheader("Release benchmark analytics")
    st.caption("Historical synthetic engine measurements; separate from the live simulation book.")
    if st.button("Run Release Benchmarks", type="primary", key="run_benchmarks"):
        run_benchmarks()
    if st.session_state.benchmark_error:
        st.error(st.session_state.benchmark_error)
    if not st.session_state.benchmark_runs:
        st.info("No benchmark results recorded in this browser session. Run the Release benchmarks above.")
        return

    runs = st.session_state.benchmark_runs
    st.caption(f"Recorded benchmark runs in this session: {len(runs)}")
    latest = runs[-1]
    st.caption(f"Latest run: {latest['recorded_at']} · {latest['platform']} · Python {latest['python']}")
    rows = []
    for run_number, run_record in enumerate(runs, start=1):
        for row in run_record["run"].workloads:
            rows.append({**row, "run": run_number})
    units = list(dict.fromkeys(row["unit"] for row in rows))
    for unit in units:
        unit_rows = [row for row in rows if row["unit"] == unit]
        st.markdown(f"**Throughput · {unit}**")
        if len(runs) > 1:
            figure = px.box(unit_rows, x="name", y="throughput", points="all", color="name",
                            custom_data=["operations", "run"])
        else:
            figure = px.bar(unit_rows, x="name", y="throughput", color="name",
                            custom_data=["operations", "run"])
        figure.update_layout(template="plotly_dark", paper_bgcolor="#111827",
                             plot_bgcolor="#111827", showlegend=False,
                             yaxis_title=unit, xaxis_title="Workload")
        figure.update_traces(hovertemplate=(
            "%{x}<br>Throughput: %{y:,.0f} " + unit
            + "<br>Operations: %{customdata[0]:,}<br>Run: %{customdata[1]}<extra></extra>"
        ))
        st.plotly_chart(figure, use_container_width=True)

    latency = latest["run"].latency
    if latency:
        percentiles = [(key, latency[key]) for key in ("p50", "p90", "p95", "p99", "p99_9") if key in latency]
        figure = go.Figure(go.Scatter(
            x=[item[0].replace("_", ".") for item in percentiles],
            y=[item[1] for item in percentiles],
            mode="lines+markers",
            line={"color": "#60A5FA", "width": 2},
            marker={"size": 8},
            hovertemplate="%{x}: %{y} ns<extra></extra>",
        ))
        figure.update_layout(template="plotly_dark", paper_bgcolor="#111827",
                             plot_bgcolor="#111827", xaxis_title="Latency percentile",
                             yaxis_title="Nanoseconds", title=f"Single-submit latency · {latency['samples']:,} samples")
        st.plotly_chart(figure, use_container_width=True)
        st.caption(f"Observed sample maximum (not a worst-case bound): {latency['sample_max']:,} ns")

    cancel_rows = [{"run": index + 1, "throughput": record["run"].cancellation["throughput"],
                    "operations": record["run"].cancellation["operations"]}
                   for index, record in enumerate(runs)]
    st.markdown("**Cancellation throughput · cancellations/s**")
    cancel_figure = px.bar(cancel_rows, x="run", y="throughput",
                           labels={"run": "Run", "throughput": "Cancellations/s"},
                           custom_data=["operations"])
    cancel_figure.update_traces(hovertemplate=(
        "Run %{x}<br>Throughput: %{y:,.0f} cancellations/s"
        "<br>Operations: %{customdata[0]:,}<extra></extra>"
    ))
    cancel_figure.update_layout(template="plotly_dark", paper_bgcolor="#111827",
                                plot_bgcolor="#111827")
    st.plotly_chart(cancel_figure, use_container_width=True)
    st.caption(f"Cancellation workload: {latest['run'].cancellation['operations']:,} cancellations per run. "
               "Setup is outside the timed interval.")


def main() -> None:
    st.set_page_config(page_title="LOB Terminal", page_icon="📈", layout="wide")
    initialize_state()
    st.markdown(CSS, unsafe_allow_html=True)

    with st.sidebar:
        st.markdown("### SIMULATION CONTROL")
        st.caption(f"Adapter: `{PROTOCOL_EXE}`")
        if st.button("Start / Reset Engine", use_container_width=True):
            start_or_reset_engine()
        depth = st.selectbox("Visible market depth", [5, 10, 20, 50], index=1)
        st.markdown("---")
        st.markdown("### DETERMINISTIC SCENARIO")
        if st.button("Load Example Scenario", use_container_width=True):
            start_or_reset_engine()
            if active_client() is not None:
                st.session_state.scenario_actions = list(SCENARIO)
                st.session_state.scenario_index = 0
                st.session_state.scenario_mode = True
                set_flash("info", "Scenario loaded. Step through actions or run them to completion.")
        scenario_remaining = len(st.session_state.scenario_actions) - st.session_state.scenario_index
        scenario_columns = st.columns(2)
        if scenario_columns[0].button("Step", disabled=scenario_remaining <= 0, use_container_width=True):
            try:
                apply_scenario_action(st.session_state.scenario_actions[st.session_state.scenario_index])
                st.session_state.scenario_index += 1
            except EngineProcessError as error:
                st.session_state.engine_error = str(error)
                set_flash("error", str(error))
        if scenario_columns[1].button("Run All", disabled=scenario_remaining <= 0, use_container_width=True):
            try:
                while st.session_state.scenario_index < len(st.session_state.scenario_actions):
                    apply_scenario_action(st.session_state.scenario_actions[st.session_state.scenario_index])
                    st.session_state.scenario_index += 1
                set_flash("success", "Deterministic scenario completed.")
            except EngineProcessError as error:
                st.session_state.engine_error = str(error)
                set_flash("error", str(error))
        if st.session_state.scenario_actions:
            st.caption(f"Scenario progress: {st.session_state.scenario_index}/{len(st.session_state.scenario_actions)}")

    flash = st.session_state.flash
    if flash:
        getattr(st, flash[0], st.info)(flash[1])
        st.session_state.flash = None

    snapshot = snapshot_now()
    render_header()
    if st.session_state.engine_error:
        st.error(f"Engine error: {st.session_state.engine_error}")
    if snapshot is None:
        st.info("Engine stopped. Start or reset it from the sidebar to begin a session-scoped simulation.")

    overview_tab, trading_tab, benchmark_tab, diagnostics_tab = st.tabs(
        ["Market Overview", "Order Entry & Tape", "Benchmark Analytics", "Events & Diagnostics"]
    )
    with overview_tab:
        st.subheader("Market overview")
        if snapshot is None or snapshot.quote is None:
            st.markdown('<div class="empty-book">No active engine session</div>', unsafe_allow_html=True)
        else:
            render_metrics(snapshot)
            st.markdown("### Order book ladder")
            if not snapshot.levels:
                st.markdown('<div class="empty-book">The order book is empty</div>', unsafe_allow_html=True)
            else:
                render_ladder(snapshot.levels, snapshot.quote, depth)
                st.markdown("### Cumulative market depth")
                st.plotly_chart(depth_figure(snapshot.levels, snapshot.quote, depth), use_container_width=True)
            if snapshot.quote.bid is not None and snapshot.quote.ask is not None:
                st.caption(f"Spread: {snapshot.quote.ask - snapshot.quote.bid} integer ticks. "
                           "No currency tick size is assumed.")

    with trading_tab:
        st.subheader("Submit limit order")
        with st.form("submit_order_form", clear_on_submit=False):
            side = st.selectbox("Side", ["BUY", "SELL"])
            first, second, third = st.columns(3)
            order_id_text = first.text_input("Order ID", value="1", key="manual_order_id")
            price_text = second.text_input("Price (integer ticks)", value="100", key="manual_price")
            quantity_text = third.text_input("Quantity", value="1", key="manual_quantity")
            submitted = st.form_submit_button("Submit Order", type="primary")
        if submitted:
            try:
                order_id = integer_input(order_id_text, "Order ID")
                price = integer_input(price_text, "Price")
                quantity = integer_input(quantity_text, "Quantity")
                response = ensure_engine().submit(order_id, side, price, quantity)
                record_response(response, f"SUBMIT {order_id} {side} {price} {quantity}")
                if response.status == "OK":
                    set_flash("success", f"Order {order_id} accepted; {len(response.trades)} trade(s).")
                else:
                    set_flash("error", f"Order rejected: {response.reason}")
                st.rerun()
            except (ValueError, EngineProcessError) as error:
                st.session_state.engine_error = str(error) if isinstance(error, EngineProcessError) else None
                set_flash("error", str(error))
                st.rerun()

        st.subheader("Cancel resting order")
        with st.form("cancel_order_form"):
            cancel_id_text = st.text_input("Existing Order ID", value="", key="cancel_order_id")
            cancelled = st.form_submit_button("Cancel Order")
        if cancelled:
            try:
                order_id = integer_input(cancel_id_text, "Order ID")
                response = ensure_engine().cancel(order_id)
                record_response(response, f"CANCEL {order_id}")
                set_flash("success" if response.status == "OK" else "error",
                          f"Cancellation {response.status.lower()}: {response.reason or order_id}")
                st.rerun()
            except (ValueError, EngineProcessError) as error:
                st.session_state.engine_error = str(error) if isinstance(error, EngineProcessError) else None
                set_flash("error", str(error))
                st.rerun()

        st.markdown("### Recent executions")
        if st.session_state.trade_history.rows:
            st.dataframe(list(reversed(st.session_state.trade_history.rows)), hide_index=True,
                         use_container_width=True)
        else:
            st.info("No trades have executed in this simulation session.")

    with benchmark_tab:
        render_benchmarks()

    with diagnostics_tab:
        st.subheader("Engine events")
        if st.session_state.event_rows:
            st.dataframe(list(reversed(st.session_state.event_rows)), hide_index=True,
                         use_container_width=True)
        else:
            st.info("No market-data events recorded in this session.")
        st.subheader("Diagnostics")
        st.write(f"Protocol executable: `{PROTOCOL_EXE}`")
        st.write(f"Engine binary present: {'yes' if PROTOCOL_EXE.is_file() else 'no'}")
        client = st.session_state.engine_client
        status = "Running" if client is not None and client.alive else "Error" if st.session_state.engine_error else "Stopped"
        st.write(f"Session status: **{status}**")
        st.write(f"Last command: `{st.session_state.last_command}`")
        st.write(f"Last engine error: {st.session_state.engine_error or 'None'}")
        st.write("C++ test status: Not run by this dashboard; use the documented CTest command.")
        if snapshot is not None:
            st.write(f"Resting orders currently reported by C++: {snapshot.order_count}")
        if st.session_state.action_rows:
            st.markdown("#### Recent submitted actions")
            st.dataframe(list(reversed(st.session_state.action_rows[-25:])), hide_index=True,
                         use_container_width=True)


if __name__ == "__main__":
    main()
