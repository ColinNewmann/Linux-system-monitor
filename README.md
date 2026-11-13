# Linux System Monitor

**A Concurrent Process-Based Monitoring Service**

A C application for querying Linux system information over TCP. The server forks one child per connection; the client supports an interactive menu, one-shot queries, and periodic monitoring.

## Requirements

- Linux or WSL2 Ubuntu with `/proc`.
- GCC, GNU Make, and glibc.
- Python 3 (standard library only), `ldd`, and `readelf` for tests.
- GDB for the full verification workflow; tcpdump for packet analysis.

## Build and run

Run commands from the project root:

```bash
make
build/protected-debug1/bin/server --port 8080 --log-level 2 --timeout-ms 30000
```

In another terminal:

```bash
build/protected-debug1/bin/client
build/protected-debug1/bin/client --command GET_STATUS
build/protected-debug1/bin/client --monitor --interval-ms 2000
```

The menu provides status, CPU, memory, uptime, and continuous monitoring. Ctrl+C stops monitoring or shuts down the server and its children.

| Command | Result |
|---|---|
| `GET_STATUS` | System overview |
| `GET_CPU` | CPU utilization and sampling interval |
| `GET_MEM` | Total, available, and used memory; utilization |
| `GET_UPTIME` | System uptime in seconds |

CPU utilization uses two samples approximately 200 ms apart. Used memory is `MemTotal - MemAvailable`. Under WSL2, measurements describe the Linux environment.

## Options

| Option | Program | Default / purpose |
|---|---|---|
| `--port` | Both | 8080; range 1–65535 |
| `--log-level` | Both | 1; 0 ERROR, 1 INFO, 2 DEBUG |
| `--timeout-ms` | Both | 10000; range 100–600000 ms |
| `--bind` | Server | 127.0.0.1; numeric IPv4 |
| `--host` | Client | 127.0.0.1; numeric IP |
| `--command` | Client | Run one query |
| `--monitor` | Client | Poll GET_STATUS repeatedly |
| `--interval-ms` | Client | 2000; range 100–600000 ms |
| `--count` | Client | Limit monitoring updates; omitted means unlimited |

Log levels are independent. Set the server timeout above the intended interactive idle time or monitoring interval.

## Build settings and tests

```bash
make DEBUG=0              # Compile out DEBUG messages
make DEBUG=1              # Include DEBUG messages (default)
make test-all             # Functional and integration tests
python3 tests/verify.py   # Clean build, complete tests, and GDB check
make clean               # Remove build artifacts; retain evidence
```

Executables and `libmonitor.so` are placed under `build/protected-debug1/` or `build/protected-debug0/`. Keep the `bin/` and `lib/` directories together: both programs use `$ORIGIN/../lib` to locate the shared library.

## Terminal demonstrations and screenshots

```bash
bash scripts/demo.sh                         # Interactive menu
bash scripts/demo.sh concurrency             # One demonstration
bash scripts/demo.sh --no-pause all           # Run all eight steps
bash scripts/demo.sh --live-capture capture   # Guided capture in two terminals
```

The Bash script runs real commands, displays English summaries, and pauses for terminal-window screenshots. Full logs use fixed names such as `docs/evidence/demo/build.log` and `queries.log`; rerunning a step replaces that step's previous output. The default packet step analyzes the saved capture from 2026-11-08; live capture is explicitly selected and never invokes sudo automatically.

Evidence is grouped into `verification/` (full test results), `demo/` (terminal demonstrations), and `packet-capture/` (the preserved original capture). Timestamps are recorded inside files. `python3 tests/verify.py` refreshes the verification set; individual tests update their own subdirectory.

Use `DEMO_PORT=18080` if port 8080 is occupied. A failed step exits with a nonzero status and retains its output.

- [Project report: architecture, tests, and packet analysis](docs/report.md)
- [Reproduction and screenshot guide](docs/demo_guide.md)
