# Reproduction and Screenshot Guide

## Preparation

Use a real Linux/WSL terminal window or the VS Code integrated terminal. Maximize it to approximately 120–150 columns and 40–50 rows. Screenshots should show the command, relevant output, and result inside the terminal window. Use the values from your own run in the report.

From the project root:

```bash
bash scripts/demo.sh
```

Select a menu item, or run one step directly. Each step prints English headings and actual commands, then pauses at a screenshot point. Press Enter to continue; Ctrl+C exits and cleans up a server started by the script. No desktop images are generated automatically.

```bash
bash scripts/demo.sh concurrency
bash scripts/demo.sh --no-pause all
DEMO_PORT=18080 bash scripts/demo.sh queries
```

The script is a plain-text Bash program and needs no compilation. Invoking it with `bash` requires no executable permission. It calls the existing C programs, Make targets, and tests. Full step and server logs are saved under `docs/evidence/demo/` with names such as `build.log`, `queries.log`, and `queries-server.log`. Each step replaces its own previous log. Long test output is retained there; terminal summaries are derived from the test's own result and timing files.

## Screenshot checklist

Figure numbers refer to the final [English Word report](report/Midterm_Report_EN.docx), including the three added illustrations. Run each command below in Terminal A. Take the screenshot when the step pauses; use scrollback where indicated. Existing manually started servers must not occupy the demo port. The build step removes only generated `build/` artifacts.

| Report figure | Command | Include in the screenshot | Suggested caption |
|---|---|---|---|
| 3 | `bash scripts/demo.sh build` | Compiler/linker commands, three artifacts, PASS | Clean build of the client, server, and shared library. |
| 4 | `bash scripts/demo.sh queries` | Four commands, actual OK responses, normal shutdown | System information returned by the TCP monitoring service. |
| 6 | `bash scripts/demo.sh concurrency` | Ten PIDs, request intervals, overlap, concurrent/sequential times | Concurrent processing of ten clients by separate child processes. |
| 7 | `bash scripts/demo.sh library` | Both ldd outputs, libmonitor.so, NEEDED and RUNPATH | Dynamic linkage of both executables to libmonitor.so. |
| 9 (panel A) | `bash scripts/demo.sh logging` | First pause: compile/runtime combinations and PASS | Verification of compile-time and runtime logging controls. |
| 9 (panel B) | Continue the logging step | Levels 0/1/2 and DEBUG=0 output | Client log output under different build and runtime settings. |
| 11 | `bash scripts/demo.sh robustness` | Both variants: input rejection, remaining children, SIGPIPE/EPIPE | Baseline and protected behavior for malformed input, timeouts, and disconnections. |
| Optional historical summary | `bash scripts/demo.sh capture` | First pause: original capture date, counts, query delays | Analysis of the recorded loopback TCP capture from 8 November 2026. |
| 14–15 (historical replay) | Continue the capture step | GET_MEM handshake, payloads, ACKs, FIN (use scrollback) | TCP connection establishment, request/response exchange, and normal closure. |
| Optional appendix | `bash scripts/demo.sh lifecycle` | FD count, children held during shutdown, same-port restart | Child cleanup, stable descriptors, and successful server restart. |

The later capture used in the report is preserved under `docs/evidence/packet-capture/report-capture/`. The capture menu item replays the original saved pcap; it does not create a new capture. Its counts and timing belong to the displayed recording date. For a new capture and a current tcpdump statistics screenshot, follow the next section.

## New packet capture: two terminals

Terminal A:

```bash
bash scripts/demo.sh --live-capture capture
```

The script prints an exact tcpdump command using an absolute path under `docs/evidence/demo/live-capture/`. Move any previous live pcap elsewhere before starting another capture. Copy that command to Terminal B. It has this form:

```bash
sudo tcpdump -Z "$(id -un)" -i lo -nn -s 0 -U \
  -w /absolute/project/path/docs/evidence/demo/live-capture/monitor.pcap 'tcp port 8080'
```

1. In Terminal B, enter any sudo password locally and wait for `listening on lo`.
2. In Terminal A, press Enter. The script starts its server, sends all four queries, and stops the server.
3. When prompted, press Ctrl+C in Terminal B. Photograph that terminal, including the tcpdump command and captured/received/dropped statistics.
4. Return to Terminal A and press Enter. The script decodes the new pcap, checks that all four request/response pairs are present, and displays payload packets. Photograph this output as well.

Use the tcpdump terminal and final statistics for report Figure 13, and decoded packets for Figures 14–15. Suggested caption: *Live loopback packet capture of the monitoring protocol and tcpdump capture statistics.*

The script never invokes sudo itself. Stopping Terminal A does not stop the user-owned tcpdump in Terminal B; stop that capture explicitly with Ctrl+C. Live mode requires an interactive terminal and cannot be combined with `--no-pause` or `all`.

## Optional interface and monitoring screenshot

Terminal A:

```bash
make
build/protected-debug1/bin/server --port 8080 --log-level 2 --timeout-ms 30000
```

Terminal B:

```bash
build/protected-debug1/bin/client
```

Select queries 1–4, then continuous monitoring (5), or use:

```bash
build/protected-debug1/bin/client --monitor --interval-ms 2000 --count 3
```

Photograph the menu or repeated status updates. Stop the manual server with Ctrl+C after use. Suggested caption: *Interactive queries and periodic client-side monitoring.*

## Tests and evidence

```bash
make test-all
python3 tests/verify.py
```

The second command performs a clean build and GDB smoke check as well. Individual targets include `test`, `test-concurrent`, `test-linkage`, `test-logging`, `test-robustness`, `test-lifecycle`, `test-proc-errors`, and `test-io`.

The evidence layout is fixed:

```text
docs/evidence/
├── verification/
│   ├── transcript.txt          # Complete clean-build and test run
│   ├── result.json             # Status and UTC start/end times
│   ├── source-sha256.json
│   └── tests/                  # basic, concurrency, logging, robustness, ...
├── demo/
│   ├── build.log               # One log per demonstration step
│   ├── queries.log
│   ├── queries-server.log
│   ├── script-checks.log       # Interactive and error-path checks
│   └── tests/                  # Detailed results used by demo summaries
└── packet-capture/
    ├── monitor.pcap            # Original capture, preserved across test runs
    ├── packets.txt
    ├── packets-hex.txt
    ├── analysis.json
    ├── traffic.txt
    ├── server.log
    ├── tcpdump-stats.txt
    └── metadata.json
```

Test folders contain `transcript.txt`, `result.json`, and descriptive server logs such as `server-baseline.log` or `server-debug1-level2.log`. Concurrency adds `timings.json`. The optional live pcap is stored under `demo/live-capture/`; its command, server, and decoded-output logs remain in `demo/`. The analyzer is `scripts/analyze_capture.py`, outside the evidence data.

Each test or demo keeps only its latest attempt, including failures. Run these workflows sequentially. A full `python3 tests/verify.py` replaces `verification/`; demo tests are isolated under `demo/tests/`. Copy any screenshots or logs you want to retain before rerunning, and update report measurements to match the selected run. The [project report](report.md) links to these fixed locations.

If a port is busy, stop your manual server or choose `DEMO_PORT=18080`. If socket or capture permissions are denied, use an appropriate WSL terminal. Increase the server timeout for longer interactive idle periods. A failed demo preserves its log and exits without printing a success result.
