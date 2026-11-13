#!/usr/bin/env bash
# 預設不呼叫 sudo；僅在使用者明確設定 MONITOR_CAPTURE_SUDO=1 時使用。
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
port=${MONITOR_CAPTURE_PORT:-8080}
case "$port" in ''|*[!0-9]*) echo 'Invalid capture port' >&2; exit 2;; esac
if ((port < 1024 || port > 65535)); then echo 'Capture port must be 1024..65535' >&2; exit 2; fi
evidence=docs/evidence/demo/live-capture
if [[ -e "$evidence/monitor.pcap" ]]; then
    echo 'Move the previous demo/live-capture/monitor.pcap before starting another capture.' >&2; exit 2
fi
mkdir -p "$evidence"
exec > >(tee "$evidence/transcript.txt") 2>&1
set -x
server_pid=''
capture_pid=''
cleanup() {
    set +e
    if [[ -n "$server_pid" ]]; then kill -TERM "$server_pid" 2>/dev/null; wait "$server_pid"; fi
    if [[ -n "$capture_pid" ]]; then kill -INT "$capture_pid" 2>/dev/null; wait "$capture_pid"; fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
capture_command=(tcpdump)
if [[ ${MONITOR_CAPTURE_SUDO:-0} == 1 ]]; then capture_command=(sudo -n tcpdump -Z "$(id -un)"); fi
"${capture_command[@]}" -i lo -nn -s 0 -U -w "$evidence/monitor.pcap" "tcp port $port" 2>"$evidence/tcpdump.log" &
capture_pid=$!
for _ in {1..50}; do
    if ! kill -0 "$capture_pid" 2>/dev/null; then
        cat "$evidence/tcpdump.log"
        echo 'BLOCKED: tcpdump could not start; capture permission is required.'
        exit 77
    fi
    if grep -q 'listening on' "$evidence/tcpdump.log"; then break; fi
    sleep 0.1
done
if ! grep -q 'listening on' "$evidence/tcpdump.log"; then echo 'FAIL: capture startup timeout'; exit 1; fi
build/protected-debug1/bin/server --port "$port" --log-level 2 2>"$evidence/server.log" &
server_pid=$!
for _ in {1..50}; do
    if ! kill -0 "$server_pid" 2>/dev/null; then cat "$evidence/server.log"; exit 1; fi
    if grep -q 'listening address=' "$evidence/server.log"; then break; fi
    sleep 0.1
done
for command in GET_STATUS GET_CPU GET_MEM GET_UPTIME; do
    build/protected-debug1/bin/client --port "$port" --command "$command" --log-level 2
done
kill -TERM "$server_pid"
wait "$server_pid"
server_pid=''
# 等待 loopback 的最後 FIN 進入擷取程序。
sleep 1
kill -INT "$capture_pid"
wait "$capture_pid"
capture_pid=''
tcpdump -nn -tttt -A -r "$evidence/monitor.pcap" > "$evidence/packets.txt" 2> "$evidence/read.log"
cat "$evidence/tcpdump.log"
if ! grep -q 'GET_MEM' "$evidence/packets.txt" || ! grep -q 'OK MEM' "$evidence/packets.txt"; then
    echo 'FAIL: request/response missing in capture'; exit 1
fi
echo "PASS: capture; evidence=$evidence"
