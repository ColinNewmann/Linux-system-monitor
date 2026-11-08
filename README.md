# Linux System Monitor

**A Concurrent Process-Based Monitoring Service** — Network and System Programming 期中專案。

以 C 與 Linux/POSIX API 實作 TCP Client／Server。Parent 接受連線，每個連線以 `fork()` 建立獨立 Child；Client 透過換行文字協定查詢 WSL Linux 的 CPU、記憶體及 uptime。

## 環境需求

- Linux 或 WSL2 Ubuntu，具備 `/proc`。
- GCC、GNU Make、glibc；測試另需 Python 3（僅標準函式庫）、`ldd`、`readelf`。
- 封包展示另需 tcpdump 與 loopback 擷取權限；GDB 用於除錯展示。
- 應用程式不需要第三方套件、Windows API 或系統函式庫設定變更。

本工作區已確認為 `/home/p1yan/mid-project`，Ubuntu 26.04.1 LTS／WSL2，GCC 15.2.0。測試執行環境若限制 socket，必須使用允許本機 TCP 的終端機；測試遇到作業系統拒絕權限會記錄 `BLOCKED`，不視為通過。

## 建置

```bash
cd /home/p1yan/mid-project
make
```

預設輸出：

```text
build/protected-debug1/bin/server
build/protected-debug1/bin/client
build/protected-debug1/lib/libmonitor.so
```

```bash
make DEBUG=0          # 移除 DEBUG 日誌呼叫，保留 ERROR／INFO
make DEBUG=1          # 編入 DEBUG 日誌呼叫（預設）
make clean           # 只刪除 build/，保留原始碼與測試證據
```

編譯設定使用不同目錄，不會互相沿用 object。兩個 executable 透過 `$ORIGIN/../lib` 尋找 `libmonitor.so`，搬移時保留 `bin/` 與 `lib/` 的相對位置即可。

## 執行

終端機 A：

```bash
build/protected-debug1/bin/server --bind 127.0.0.1 --port 8080 --log-level 2 --timeout-ms 30000
```

終端機 B：

```bash
build/protected-debug1/bin/client --port 8080
build/protected-debug1/bin/client --port 8080 --command GET_STATUS
build/protected-debug1/bin/client --port 8080 --command GET_CPU
build/protected-debug1/bin/client --port 8080 --command GET_MEM
build/protected-debug1/bin/client --port 8080 --command GET_UPTIME
build/protected-debug1/bin/client --port 8080 --monitor --interval-ms 2000 --log-level 0
```

互動選單提供四種查詢、連續監控與離開。Ctrl+C 可停止連續監控並關閉連線；對 Server 使用 Ctrl+C 則停止接受連線並回收 Child。

回應格式範本（數值由執行時決定）：

```text
OK CPU usage_pct=<percent> sample_ms=<milliseconds>
OK MEM total_kb=<kB> available_kb=<kB> used_kb=<kB> used_pct=<percent>
OK UPTIME seconds=<seconds>
```

所有協定訊息最後都有換行。CPU 以兩次 `/proc/stat` 取樣、相隔約 200 ms 計算；記憶體 used 為 `MemTotal - MemAvailable`。這些數值代表 WSL Linux，不等於整台 Windows Host。

### 參數

| 參數 | Server | Client |
|---|---|---|
| `--port` | 1～65535；預設 8080 | 同左 |
| `--log-level` | 0 ERROR／1 INFO／2 DEBUG；預設 1 | 獨立設定；預設 1 |
| `--timeout-ms` | 每筆完整 request 的期限，亦用於傳送；預設 10000 | 連線、傳送及接收各階段期限；預設 10000 |
| `--bind` | 數字 IPv4；預設 127.0.0.1 | — |
| `--host` | — | 數字 IP；預設 127.0.0.1 |
| `--command` | — | 單次查詢，也可用無效指令觀察錯誤 |
| `--monitor` | — | 持續輪詢 GET_STATUS |
| `--interval-ms` | — | 100～600000；預設 2000 |
| `--count` | — | 監控更新次數；省略代表持續執行 |

timeout 可設定為 100～600000 ms。Server 會關閉長時間沒有完成 request 的連線，所以互動等待或監控間隔較長時，須提高 Server 的 timeout。監控取樣耗時算入更新間隔；若取樣已超過間隔，完成後直接進行下一次查詢，不額外忙等。

## 測試

在允許 socket 的一般 WSL 終端機執行，測試會自行啟動 Server 並選擇臨時埠，不需先手動啟動服務：

```bash
make test
make test-concurrent
make test-robustness
make test-logging
make test-linkage
make test-lifecycle
make test-proc-errors
make test-io
make test-all
```

以上展示測試使用預設 `DEBUG=1`；並行與 TCP reset 測試會讀取 DEBUG 事件作為證據。`test-logging` 自動建立並比較 DEBUG=0／1 與兩端各三種 runtime 層級。

完整 clean build、測試與 GDB 紀錄：

```bash
python3 tests/verify.py
```

每次 Python 測試建立 `docs/evidence/<UTC時間>-<測試名稱>/`，保存完整指令、輸出、Server 日誌與結果 JSON；完整驗證另附原始碼 SHA-256。Make 遇到失敗或權限阻擋會回傳非零，詳細狀態以 `result.json` 為準。證據時間使用 UTC，台灣時間加 8 小時。

Robustness 會另建 `build/baseline-debug1/`；Baseline 僅供三組對照，限定 loopback，故意採寬鬆前綴解析、取消讀取期限、不抑制 SIGPIPE。預設 protected 版本保留所有防護，不會因執行 Baseline 測試而被替換。

## 封包擷取

```bash
make capture
```

預設只用目前帳號啟動 tcpdump。若缺少擷取權限會記錄原因並停止。使用者明確授權後，才可執行：

```bash
MONITOR_CAPTURE_SUDO=1 make capture
```

此選項僅對 tcpdump 使用 `sudo -n`，不對 Server／Client 提權；透過 `-Z` 降回目前使用者寫入證據。若本機需要密碼，請在自己的互動終端機先完成 sudo 驗證，不要把密碼提供給代理。腳本不安裝套件，也不修改系統設定。預設埠 8080 必須未被其他服務使用，可設定 `MONITOR_CAPTURE_PORT=18080`。

詳見 [系統設計](docs/design.md)、[實際測試結果](docs/test_results.md)、[需求追蹤矩陣](docs/requirements_matrix.md) 及 [課堂展示指南](docs/demo_guide.md)。

R1～R9 均已有實際驗證紀錄。已保存 [原始 pcap](docs/evidence/authorized-capture/monitor.pcap) 與 [封包分析](docs/evidence/authorized-capture/packet_analysis.md)；此次成功擷取使用操作者在 WSL 終端機執行的手動 tcpdump，與自動腳本的驗證狀態分開記錄。
