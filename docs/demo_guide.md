# 操作與展示指南

以下指令均在專案根目錄的 Linux／WSL 終端機執行。

## 建置與查詢

```bash
make clean
make -j2
build/protected-debug1/bin/server --port 8080 --log-level 2 --timeout-ms 30000
```

另一個終端機執行：

```bash
build/protected-debug1/bin/client --command GET_STATUS
build/protected-debug1/bin/client --command GET_CPU
build/protected-debug1/bin/client --command GET_MEM
build/protected-debug1/bin/client --command GET_UPTIME
build/protected-debug1/bin/client --monitor --interval-ms 2000
```

省略 `--command` 與 `--monitor` 可開啟選單。Ctrl+C 結束監控或停止 Server。

## 功能驗證

| 指令 | 驗證內容 |
|---|---|
| `make test` | 查詢、選單、監控與協定邊界 |
| `make test-concurrent` | 十條連線、Child PID、處理區間與循序耗時比較 |
| `make test-linkage` | 兩端的 libmonitor.so、DT_NEEDED 與 RUNPATH |
| `make test-logging` | 兩種編譯設定與兩端三種日誌層級 |
| `make test-robustness` | 非法／超長輸入、逾時、SIGPIPE 與 TCP reset |
| `make test-lifecycle` | FD 穩定性、Child 回收、關機與同埠重啟 |
| `make test-proc-errors` | 系統資料讀取失敗的錯誤回應 |
| `make test-io` | Partial write 與寫入期限 |

測試會自行啟動臨時埠 Server，紀錄保存於 `docs/evidence/`。完整 clean build、測試與 GDB 檢查可使用 `python3 tests/verify.py`。

並行測試以 barrier 同步十筆 CPU 查詢，輸出 Child PID、起訖時間與 `timings.json`。防護比較的 Baseline 與 Protected 分開建置；SIGPIPE probe 在 Python 中以 `exit=-13` 表示，TCP reset 則依實際 errno 記錄。

## 日誌展示

Server 保持執行時，比較 Client 輸出：

```bash
make DEBUG=0
build/protected-debug1/bin/client --command GET_MEM --log-level 0
build/protected-debug1/bin/client --command GET_MEM --log-level 1
build/protected-debug1/bin/client --command GET_MEM --log-level 2
build/protected-debug0/bin/client --command GET_MEM --log-level 2
```

DEBUG=0 的程式只輸出 ERROR／INFO；Server 的層級可獨立設定。

## 封包擷取

自動擷取需先停止占用 8080 的 Server：

```bash
make capture
```

需要 sudo 時，先在同一互動終端機完成驗證，再執行 `MONITOR_CAPTURE_SUDO=1 make capture`。腳本使用 `sudo -n`；可設定 `MONITOR_CAPTURE_PORT=18080` 改用其他埠。

手動擷取可在第三個終端機執行：

```bash
mkdir -p docs/evidence/manual-capture
sudo tcpdump -Z "$(id -un)" -i lo -nn -s 0 -U \
  -w docs/evidence/manual-capture/monitor.pcap 'tcp port 8080'
```

啟動 Server 並執行查詢後，以 Ctrl+C 停止 tcpdump、保存統計，再離線解碼：

```bash
TZ=UTC tcpdump -nn -tttt -A -r docs/evidence/manual-capture/monitor.pcap
```

觀察三向交握、查詢與回應 payload，以及 FIN／ACK。TCP segment 與應用訊息不保證一對一。既有實測見 [封包分析](evidence/authorized-capture/packet_analysis.md)。

## GDB

```bash
gdb --nx build/protected-debug1/bin/server
```

```text
set args --port 18080 --log-level 2
set follow-fork-mode child
set detach-on-fork on
break serve_client
run
```

另一個終端機執行 `build/protected-debug1/bin/client --port 18080 --command GET_MEM`，於 GDB 查看 `bt` 並 `continue`。結束後停止已 detach 的 Parent。

埠衝突時改用其他埠；互動等待逾時則提高 Server 的 `--timeout-ms`。測試及擷取需在允許 socket 與相應擷取權限的環境執行。
