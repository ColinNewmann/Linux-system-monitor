#!/usr/bin/env bash
# 分段執行真實展示；完整輸出保存在 evidence，終端機顯示適合截圖的摘要。
set -Eeuo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."

usage() {
    cat <<'EOF'
Usage: bash scripts/demo.sh [--no-pause] [--live-capture] [STEP]

STEP: build, queries, concurrency, library, logging, robustness,
      capture, lifecycle, all (omit STEP for the interactive menu)

  --no-pause      Run a selected step or all steps without screenshot pauses.
  --live-capture  Guide a new capture in another terminal (capture step only).
  --help         Show this help.

DEMO_PORT=18080 changes the query/live-capture port (default: 8080).
The capture step normally analyzes the saved, dated project pcap.
The script never invokes sudo or takes desktop screenshots.
EOF
}

pause_enabled=1
live_capture=0
step=''
while (($#)); do
    case "$1" in
        --help|-h) usage; exit 0 ;;
        --no-pause) pause_enabled=0 ;;
        --live-capture) live_capture=1 ;;
        build|queries|concurrency|library|logging|robustness|capture|lifecycle|all)
            if [[ -n "$step" ]]; then usage >&2; exit 2; fi
            step=$1 ;;
        *) usage >&2; exit 2 ;;
    esac
    shift
done
if [[ -z "$step" && ! -t 0 ]]; then
    echo 'Select a step explicitly when standard input is not a terminal.' >&2; exit 2
fi
if ((live_capture)) && { [[ "$step" != capture ]] || ((pause_enabled == 0)) || [[ ! -t 0 ]]; }; then
    echo '--live-capture requires the capture step and an interactive terminal.' >&2; exit 2
fi
if [[ ! -t 0 ]]; then pause_enabled=0; fi
port=${DEMO_PORT:-8080}
if [[ ! "$port" =~ ^[0-9]{1,5}$ ]]; then echo 'Invalid DEMO_PORT.' >&2; exit 2; fi
port=$((10#$port))
if ((port < 1024 || port > 65535)); then echo 'DEMO_PORT must be 1024..65535.' >&2; exit 2; fi

session=docs/evidence/demo
mkdir -p "$session"
# 展示測試寫在 demo/tests，完整驗證保留在 verification。
export MONITOR_EVIDENCE_ROOT="$session"
server_pid=''
step_log="$session/session.log"
client=build/protected-debug1/bin/client

cleanup() {
    if [[ -n "$server_pid" ]]; then
        kill -TERM "$server_pid" 2>/dev/null || true
        wait "$server_pid" 2>/dev/null || true
        server_pid=''
    fi
}
fail() {
    local status=$1
    trap - ERR
    printf '\nFAILED (exit %s). Review %s\n' "$status" "$step_log" >&2
    exit "$status"
}
trap cleanup EXIT
trap 'fail $?' ERR
trap 'exit 130' INT
trap 'exit 143' TERM

say() { printf '%s\n' "$*" | tee -a "$step_log"; }
show_command() {
    { printf '$'; printf ' %q' "$@"; printf '\n'; } | tee -a "$step_log"
}
run() {
    show_command "$@"
    "$@" 2>&1 | tee -a "$step_log"
}
run_logged() {
    show_command "$@"
    local status
    if "$@" >>"$step_log" 2>&1; then return 0; else status=$?; fi
    tail -n 45 "$step_log" >&2
    return "$status"
}
pause_for_screenshot() {
    if ((pause_enabled)); then
        printf '\nCapture this terminal window, then press Enter to continue... '
        IFS= read -r _ || exit 0
    fi
}
ensure_build() { run_logged make DEBUG=1 VARIANT=protected all; }

start_server() {
    local log="$session/$current_item-server.log"
    show_command build/protected-debug1/bin/server --port "$port" --log-level 2 --timeout-ms 30000
    build/protected-debug1/bin/server --port "$port" --log-level 2 --timeout-ms 30000 >"$log" 2>&1 &
    server_pid=$!
    for _ in {1..50}; do
        if ! kill -0 "$server_pid" 2>/dev/null; then
            cat "$log" >&2
            say 'Server startup failed. Stop your old server or set DEMO_PORT.'
            return 1
        fi
        if grep -q 'listening address=' "$log"; then
            say "Server PID=$server_pid; port=$port; log=$log"
            return 0
        fi
        sleep .1
    done
    say 'Server startup timed out.'; return 1
}
stop_server() {
    show_command kill -TERM "$server_pid"
    kill -TERM "$server_pid"
    wait "$server_pid"
    say 'Server exited normally; its children have been reaped.'
    server_pid=''
}
query_traffic() {
    start_server
    for command in GET_STATUS GET_CPU GET_MEM GET_UPTIME; do
        run "$client" --port "$port" --command "$command" --log-level 0
    done
    stop_server
}

test_summary() {
    python3 - "$step_log" "$1" <<'PY' | tee -a "$step_log"
import json
from pathlib import Path
import re
import sys
log, kind = Path(sys.argv[1]), sys.argv[2]
text = log.read_text()
matches = re.findall(r'^evidence: (.+)$', text, re.M)
assert matches, 'test evidence directory missing'
folder = Path(matches[-1])
result = json.loads((folder / 'result.json').read_text())
assert result['status'] == 'PASS', result
transcript = (folder / 'transcript.txt').read_text()
print(f'\nVerified result: {result["status"]}; evidence: {folder}')
if kind == 'concurrency':
    data = json.loads((folder / 'timings.json').read_text())
    print('Child PIDs:', ', '.join(map(str, data['pids'])))
    print('PID       Request start (ms)   Request end (ms)')
    for pid in data['pids']:
        print(f'{pid:<9} {data["server_starts_ms"][str(pid)]:<20} {data["server_ends_ms"][str(pid)]}')
    print(f'Concurrent: {data["concurrent_s"]:.6f} s; sequential: {data["sequential_s"]:.6f} s')
    print(f'Common overlap of all ten server intervals: {data["server_overlap_ms"]} ms')
elif kind == 'robustness':
    for variant in ['baseline', 'protected']:
        segment = transcript.split(f'=== {variant}: Test A', 1)[1]
        if variant == 'baseline':
            segment = segment.split('=== protected: Test A', 1)[0]
        print(f'\n{variant.upper()}')
        for response in re.findall(r'^request=.* response=(.+)$', segment, re.M)[:2]:
            print('Input validation:', response)
        observation = re.search(r'^observed_s=.+$', segment, re.M)
        assert observation
        print('Silent client:', observation[0])
        probe = segment.split('deterministic shared send path ===', 1)[1].split('TCP reset integration', 1)[0]
        for line in probe.splitlines():
            if line.startswith(('result=', 'exit=')):
                print('Closed-peer send:', line)
        reset = re.search(r'send_failed errno=\d+ message=[^\n]+', segment)
        if reset:
            print('TCP reset:', reset[0])
        print('Subsequent GET_MEM: verified by the test')
elif kind == 'logging':
    for line in transcript.splitlines():
        if line.startswith('=== compile'):
            print(line)
    print('Each server configuration checked client levels 0, 1, and 2.')
elif kind == 'lifecycle':
    for line in transcript.splitlines():
        if any(token in line for token in ['fd_before=', 'shutdown with held children:', 'restarted successfully']):
            print(line)
else:
    raise AssertionError(kind)
PY
}

capture_demo() {
    local pcap=docs/evidence/packet-capture/monitor.pcap
    if ((live_capture)); then
        pcap="$session/live-capture/monitor.pcap"
        mkdir -p "$session/live-capture"
        if [[ -e "$pcap" ]]; then
            say 'Move the previous live-capture/monitor.pcap before starting another capture.'
            return 1
        fi
        say 'Terminal B: run this command and keep tcpdump running:'
        show_command sudo tcpdump -Z "$(id -un)" -i lo -nn -s 0 -U -w "$PWD/$pcap" "tcp port $port"
        printf '\nWhen Terminal B reports "listening on lo", press Enter here... '
        IFS= read -r _ || exit 0
        [[ -f "$pcap" ]] || { say 'No capture file found.'; return 1; }
        ensure_build
        query_traffic
        printf '\nStop tcpdump in Terminal B with Ctrl+C and photograph its statistics.\nThen press Enter here... '
        IFS= read -r _ || exit 0
    else
        say 'Recorded capture from 2026-11-08; this step does not start a new capture.'
    fi
    run_logged env TZ=UTC tcpdump -nn -tttt -A -r "$pcap"
    if ((live_capture == 0)); then
        run_logged python3 scripts/analyze_capture.py
        # 摘要直接來自 pcap 分析結果，不以固定展示數字代替實測。
        python3 - <<'PY' | tee -a "$step_log"
import json
from pathlib import Path
folder = Path('docs/evidence/packet-capture')
data = json.loads((folder / 'analysis.json').read_text())
print('Saved pcap:', folder / data['pcap'])
print(f'Packets={data["packet_count"]}; connections={data["connections"]}; FIN={data["fin"]}; RST={data["rst"]}')
for flow in data['flows']:
    if flow['kind'] == 'query':
        print(f'{flow["command"]:<12} client_port={flow["client_port"]} request={flow["request_bytes"]} B '
              f'response={flow["response_bytes"]} B delay={flow["request_to_response_ms"]:.3f} ms')
print('Operator-reported statistics from the original capture:')
print('\n'.join((folder / 'tcpdump-stats.txt').read_text().splitlines()[1:]))
PY
        pause_for_screenshot
    fi
    if ((live_capture)); then
        local decoded="$session/decoded-packets.txt"
        show_command env TZ=UTC tcpdump -nn -tttt -A -r "$pcap"
        env TZ=UTC tcpdump -nn -tttt -A -r "$pcap" >"$decoded" 2>>"$step_log"
        cat "$decoded" >>"$step_log"
        python3 - "$decoded" <<'PY' | tee -a "$step_log"
from pathlib import Path
import sys
text = Path(sys.argv[1]).read_text()
for name in ['STATUS', 'CPU', 'MEM', 'UPTIME']:
    assert 'GET_' + name + '\n' in text, 'request missing: ' + name
    assert 'OK ' + name + ' ' in text, 'response missing: ' + name
print('Verified all four request/response pairs in the new capture.')
PY
        say 'New capture payloads (UTC):'
        run env TZ=UTC tcpdump -nn -tttt -A -r "$pcap" 'tcp[13] & 8 != 0'
    else
        local memory_port
        memory_port=$(python3 - <<'PY'
import json
from pathlib import Path
data = json.loads(Path('docs/evidence/packet-capture/analysis.json').read_text())
print(next(flow['client_port'] for flow in data['flows'] if flow.get('command') == 'GET_MEM'))
PY
)
        say 'Recorded GET_MEM connection: handshake, payloads, and FIN (UTC):'
        run env TZ=UTC tcpdump -nn -tttt -A -r "$pcap" "tcp port $memory_port"
    fi
}

run_step() {
    local item=$1
    current_item=$item
    step_log="$session/$item.log"
    : >"$step_log"
    say '============================================================'
    say "Linux System Monitor | ${item^^}"
    say "UTC: $(date -u +'%Y-%m-%d %H:%M:%S')"
    say "Full execution log: $step_log"
    say '============================================================'
    case "$item" in
        build)
            run make clean
            run make -j2 DEBUG=1 VARIANT=protected
            run ls -l build/protected-debug1/bin/client build/protected-debug1/bin/server build/protected-debug1/lib/libmonitor.so ;;
        queries) ensure_build; query_traffic ;;
        concurrency)
            run_logged make DEBUG=1 VARIANT=protected test-concurrent
            test_summary concurrency ;;
        library)
            ensure_build
            run ldd build/protected-debug1/bin/client
            run ldd build/protected-debug1/bin/server
            for binary in client server; do
                run_logged readelf -d "build/protected-debug1/bin/$binary"
                readelf -d "build/protected-debug1/bin/$binary" | grep -E 'NEEDED.*libmonitor|RUNPATH' | tee -a "$step_log"
            done ;;
        logging)
            run_logged make VARIANT=protected test-logging
            test_summary logging
            pause_for_screenshot
            start_server
            for level in 0 1 2; do run "$client" --port "$port" --command GET_MEM --log-level "$level"; done
            run build/protected-debug0/bin/client --port "$port" --command GET_MEM --log-level 2
            stop_server ;;
        robustness)
            run_logged make DEBUG=1 VARIANT=protected test-robustness
            test_summary robustness ;;
        capture) capture_demo ;;
        lifecycle)
            run_logged make DEBUG=1 VARIANT=protected test-lifecycle
            test_summary lifecycle ;;
    esac
    say "PASS: $item (all commands completed successfully)"
    pause_for_screenshot
}

if [[ "$step" == all ]]; then
    for item in build queries concurrency library logging robustness capture lifecycle; do run_step "$item"; done
elif [[ -n "$step" ]]; then
    run_step "$step"
else
    while true; do
        printf '\nLinux System Monitor - Demonstration\n\n'
        printf '1. Clean Build\n2. System Information Queries\n3. Ten Concurrent Clients\n'
        printf '4. Shared Library Verification\n5. Logging Controls\n6. Robustness Comparisons\n'
        printf '7. Packet Capture (recorded)\n8. Process Lifecycle\n0. Exit\n\nSelect: '
        IFS= read -r choice || break
        case "$choice" in
            1) run_step build ;; 2) run_step queries ;; 3) run_step concurrency ;;
            4) run_step library ;; 5) run_step logging ;; 6) run_step robustness ;;
            7) run_step capture ;; 8) run_step lifecycle ;; 0) break ;;
            *) echo 'Please select 0 through 8.' ;;
        esac
    done
fi
printf '\nDemonstration records: %s\n' "$session"
