# 課堂展示指南

以下指令在一般 WSL Ubuntu 終端機執行，工作目錄為 `/home/p1yan/mid-project`。不需要課程網站、網路下載或第三方 Python 套件。先確認已安裝 gcc、make、python3、gdb、tcpdump；本專案不會自行安裝。

## 1. Clean build

```bash
cd /home/p1yan/mid-project
make clean
make -j2
```

指出 `-Wall -Wextra -Wpedantic`、PIC、shared linking 及三種產物。若要一次保存整份驗證紀錄，執行 `python3 tests/verify.py`。

## 2. Server、查詢與監控

終端機 A：

```bash
build/protected-debug1/bin/server --port 8080 --log-level 2 --timeout-ms 30000
```

終端機 B：

```bash
build/protected-debug1/bin/client --command GET_STATUS --log-level 2
build/protected-debug1/bin/client --command GET_CPU
build/protected-debug1/bin/client --command GET_MEM
build/protected-debug1/bin/client --command GET_UPTIME
build/protected-debug1/bin/client
```

選單可依序選 1～4，再選 6；或選 5 連續監控，以 Ctrl+C 離開。

```bash
build/protected-debug1/bin/client --monitor --interval-ms 2000
```

說明 CPU 是約 200 ms 雙取樣，Memory 的 used 為 total−available。讀取 `/proc/meminfo`、`/proc/uptime` 作為數值參照；動態資料不要求不同時間讀取完全相等。

## 3. 十人並行

```bash
make test-concurrent
```

測試自行啟動另一個臨時埠 Server。展示輸出的十個 Child PID、各 Client 起訖時間、Server 處理區間共同重疊時間，以及並行／循序時間對照。`timings.json` 與 Server log 可供截圖。

測試使用 barrier 同時送出十筆 CPU 查詢；Server 的十個 Child 各自等待 CPU 取樣，因此能清楚看出處理重疊。

## 4. Shared Library

```bash
make test-linkage
```

指出 Client、Server 的 `libmonitor.so` 路徑、DT_NEEDED 與 RUNPATH；不需要修改全域 library 設定。

## 5. 雙層日誌

```bash
make test-logging
```

自動比較兩種 compile 設定及兩端 runtime 0／1／2。也可手動比較：

```bash
build/protected-debug1/bin/client --command GET_MEM --log-level 0
build/protected-debug1/bin/client --command GET_MEM --log-level 1
build/protected-debug1/bin/client --command GET_MEM --log-level 2
build/protected-debug0/bin/client --command GET_MEM --log-level 2
```

最後一個程式即使 runtime=2 也沒有 DEBUG。Server 同理，層級可以與 Client 分開設定。

## 6. 三組防護比較

```bash
make test-robustness
```

依序解說：

1. 不完整格式與 208-byte request：Baseline 誤接受，Protected 回傳錯誤。
2. 不送資料：固定觀察 0.8 秒，Baseline Child 仍存在；Protected 400 ms 後釋放。另測持續少量送字元仍無法重設期限。
3. 對端關閉：Baseline probe 因 SIGPIPE 結束；Protected 回傳 EPIPE。再展示 TCP reset 日誌與成功的下一筆查詢。

Python subprocess 將被 SIGPIPE 結束表示為 `exit=-13`；shell 的表示方式可能是 141，兩者不要混淆。TCP reset 的第一次 send 可能回傳 ECONNRESET，不能據此虛構 TCP Server 被 SIGPIPE 結束。

## 7. 程序清理與錯誤路徑

```bash
make test-lifecycle
make test-proc-errors
make test-io
```

展示 Parent FD 前後相同、Child 不持有 listener／self-pipe、十個連線仍開啟時關機能清理，以及相同埠重新啟動。

`test-proc-errors` 的獨立測試 binary 模擬 fopen 失敗，驗證 `ERR SYSTEM_INFO`，不修改 /proc。`test-io` 以小 socket buffer 驗證大量資料分段傳送及寫入逾時。

可選 GDB 展示：

```bash
gdb --nx build/protected-debug1/bin/server
```

於 GDB 中：

```text
set args --port 18080 --log-level 2
set follow-fork-mode child
set detach-on-fork on
break serve_client
run
```

另一終端機使用 `client --port 18080 --command GET_MEM`，再於 GDB 查看 `bt` 並 `continue`。Parent 會被 detach，結束展示時仍須在原終端機或使用已確認的 PID 停止 Parent；不要使用會影響其他服務的廣泛 pkill。

## 8. tcpdump

先對終端機 A 的 Server 按 Ctrl+C，釋放 8080。一般帳號若有擷取權限：

```bash
make capture
```

若缺少 CAP_NET_RAW，取得使用者同意後才使用：

```bash
MONITOR_CAPTURE_SUDO=1 make capture
```

腳本的 sudo 使用 `-n`，不會等待密碼。若需要密碼，使用者可在自己的互動終端機先完成 sudo 驗證，再重新執行。代理不得索取密碼。

等價的手動擷取流程，在使用者授權後於終端機 C 執行：

```bash
mkdir -p docs/evidence/manual-capture
sudo tcpdump -i lo -nn -s 0 -U -w docs/evidence/manual-capture/monitor.pcap 'tcp port 8080'
```

於 A 啟動 Server，B 執行查詢，再於 C 按 Ctrl+C 停止擷取：

```bash
tcpdump -nn -tttt -A -r docs/evidence/manual-capture/monitor.pcap
```

解說 SYN／SYN-ACK／ACK、帶換行的 GET_MEM 與 OK MEM payload，以及 FIN／ACK。TCP segment 與應用訊息不必一對一；以實際 pcap 為準。保留擷取統計，包括 dropped packets。

## 證據與異常處理

測試結果位於 `docs/evidence/`，完整命令及輸出在 transcript，Server PID 事件在 `.log`。閱讀 [test_results.md](test_results.md) 與 [requirements_matrix.md](requirements_matrix.md) 選擇實際已通過的紀錄。

- 埠衝突：停止自己的舊 Server，或選擇另一個埠；擷取可設定 `MONITOR_CAPTURE_PORT=18080`。
- Socket 被沙箱拒絕：改在允許 socket 的 WSL 終端機執行；不能以編譯成功代替通訊成功。
- Capture 權限不足：保留 BLOCKED 記錄，不宣稱 R4 通過。
- 互動等待太久被斷線：提高 Server `--timeout-ms` 並重新連線。

展示結束對所有自己啟動的 Server 按 Ctrl+C。自動測試只清理自己啟動的程序，不影響手動 Server。
