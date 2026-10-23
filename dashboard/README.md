# LOB Terminal Dashboard

The dashboard is a local Streamlit UI for the C++ limit order book. C++ remains the source of truth: Streamlit starts one `lob_protocol` subprocess per browser session and sends orders, cancellations, resets, and snapshots to that process. There is no Python matching implementation and no network service beyond Streamlit's local UI server.

Prices are exact integer ticks as defined by `lob::Price`; the UI does not assume a currency tick size. Quantities and IDs are unsigned integers. Simulation state and bounded tape/event history live only for the active Streamlit session; refreshing or ending that session does not persist the book.

## Ubuntu / WSL setup

From the repository root, install Python's venv support if needed, then create an isolated environment:

```sh
sudo apt update
sudo apt install -y python3-venv
python3 -m venv .venv
source .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install -r dashboard/requirements.txt
```

Build the C++ adapter and benchmark executables, and run C++ tests:

```sh
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --parallel
ctest --test-dir build-release --output-on-failure
```

Run Python tests, including a real adapter integration test:

```sh
LOB_PROTOCOL_EXECUTABLE="$PWD/build-release/lob_protocol" python -m unittest discover -s dashboard -p 'test_*.py' -v
```

Launch the dashboard:

```sh
python -m streamlit run dashboard/app.py
```

Open the local URL printed by Streamlit. Use **Start / Reset Engine** to start the C++ process. Submit integer-tick orders in **Order Entry & Tape**, or load the deterministic scenario in the sidebar. The **Run Release Benchmarks** button explicitly runs the existing Release executables; it is not invoked on ordinary Streamlit reruns.

## Streamlit Community Cloud

The repository root `packages.txt` installs the C++ compiler and Ninja. `dashboard/requirements.txt` supplies CMake. When no prebuilt adapter exists, the first **Start / Reset Engine** click configures and builds only the requested C++ target into the ignored `build-dashboard/` directory; this avoids committing a platform-specific binary or modifying `build-release/`. Benchmark executables are built only after clicking **Run Release Benchmarks**. Cloud instances are ephemeral, so a fresh deployment may need to build these targets again.

Deploy `dashboard/app.py` as the app entrypoint. Community Cloud expects external apt dependencies in a root-level `packages.txt` and supports a requirements file beside the app entrypoint. See the [Streamlit Community Cloud dependency guide](https://docs.streamlit.io/deploy/streamlit-community-cloud/deploy-your-app/app-dependencies).

## Local protocol

The adapter uses a versioned, line-oriented tab-delimited protocol to avoid adding a JSON dependency. Startup writes `READY<TAB>1`. Requests are `SUBMIT id BUY|SELL price quantity`, `CANCEL id`, `SNAPSHOT`, `RESET`, or `EXIT`. Each response is framed by `BEGIN<TAB>1<TAB>status<TAB>operation`, zero or more typed records (`TRADE`, `EVENT_TRADE`, `EVENT_LEVEL`, `EVENT_QUOTE`, `QUOTE`, `COUNT`, `LEVEL`), and `END`. Rejections and malformed requests are distinct statuses with a reason token; diagnostics are kept off stdout. Integer fields are parsed strictly before the C++ API is called.

The Python client uses an argument vector (not a shell command), bounded response lines, and per-read timeouts. This protocol is a local adapter interface, not a public exchange feed or a durable event stream.

## Benchmark display

The dashboard parses the existing benchmark executables' human-readable output and rejects incomplete output instead of filling missing values. Throughput units remain separated by workload category, and latency percentiles are labeled in nanoseconds. The existing C++ timing loops are unchanged. Benchmark captures and their metadata are held in the active UI session only.

For direct terminal runs:

```sh
./build-release/lob_benchmark
./build-release/cancellation_benchmark
```

## Limitations

- The book is session-scoped and in-memory; it has no persistence or recovery.
- The dashboard uses one synchronous C++ subprocess per Streamlit session and has no multi-user coordination guarantees.
- The protocol's rejection token is intentionally generic for invalid/duplicate/overflow submissions; the current engine result API does not expose a finer rejection reason.
- The bounded trade tape assigns a UI-local sequence to observed executions; it does not fabricate exchange timestamps or aggressor-side labels.
- The benchmark parser targets the current benchmark output format. Machine/build metadata is recorded from the UI host, but the C++ binaries do not currently report compiler/build metadata themselves.
- This is a research/educational visualization, not a production trading system or a performance guarantee.
