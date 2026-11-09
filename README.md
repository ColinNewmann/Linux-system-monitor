# Linux System Monitor

**A Concurrent Process-Based Monitoring Service**

以 C 與 Linux/POSIX API 開發的 TCP 系統監控服務。Server 使用 `fork()` 為每條連線建立獨立子程序；Client 提供互動選單、單次查詢與連續監控，可取得 CPU、記憶體及系統運行時間。

## 環境需求

- Linux 或 WSL2 Ubuntu，具備 `/proc`。
- 建置：GCC、GNU Make、glibc。
- 測試：Python 3 標準函式庫、`ldd`、`readelf`；完整驗證另需 GDB。
- 封包分析：tcpdump 與封包擷取權限。

## 建置與執行

以下指令均在專案根目錄執行：

```bash
make
```

啟動 Server：

```bash
build/protected-debug1/bin/server --port 8080 --log-level 2 --timeout-ms 30000
```

另一個終端機執行 Client：

```bash
build/protected-debug1/bin/client
build/protected-debug1/bin/client --command GET_STATUS
build/protected-debug1/bin/client --monitor --interval-ms 2000
```

Client 未指定查詢模式時顯示選單。Ctrl+C 結束監控；Server 收到 Ctrl+C 後關閉連線並回收子程序。

| 指令 | 查詢內容 |
|---|---|
| `GET_STATUS` | 系統摘要 |
| `GET_CPU` | CPU 使用率與取樣間隔 |
| `GET_MEM` | 記憶體總量、可用量、使用量及使用率 |
| `GET_UPTIME` | 系統運行秒數 |

CPU 使用率以約 200 ms 間隔的兩次取樣計算；記憶體使用量為 `MemTotal - MemAvailable`。WSL 中的數值代表 Linux 環境。

## 啟動參數

| 參數 | 適用程式 | 預設值／用途 |
|---|---|---|
| `--port` | 兩端 | 8080，範圍 1～65535 |
| `--log-level` | 兩端 | 1；0 ERROR、1 INFO、2 DEBUG |
| `--timeout-ms` | 兩端 | 10000；範圍 100～600000 ms |
| `--bind` | Server | 127.0.0.1，數字 IPv4 |
| `--host` | Client | 127.0.0.1，數字 IP |
| `--command` | Client | 執行一次指定查詢 |
| `--monitor` | Client | 持續查詢系統摘要 |
| `--interval-ms` | Client | 2000；範圍 100～600000 ms |
| `--count` | Client | 限制監控次數，省略則持續執行 |

兩端可獨立設定日誌層級。Server 的 timeout 限制完成一筆 request 的時間；互動等待或監控間隔較長時，應相應提高此值。

## 建置設定與測試

```bash
make DEBUG=0        # 移除 DEBUG 日誌，保留 ERROR／INFO
make DEBUG=1        # 編入 DEBUG 日誌（預設）
make test-all       # 功能、並行、防護、日誌與生命週期測試
python3 tests/verify.py  # Clean build、完整測試與 GDB 驗證
make clean          # 移除 build/，保留測試紀錄
```

產物位於 `build/protected-debug1/` 或 `build/protected-debug0/`。Client、Server 共用 `lib/libmonitor.so`，透過 `$ORIGIN/../lib` 載入；部署時保留 `bin/` 與 `lib/` 的相對位置。

個別測試與展示指令見 [操作指南](docs/demo_guide.md)。測試紀錄保存在 `docs/evidence/`，時間使用 UTC。並行及防護展示使用預設 `DEBUG=1`。

## 封包擷取

```bash
make capture
```

需要 sudo 時，在互動終端機完成驗證後執行：

```bash
MONITOR_CAPTURE_SUDO=1 make capture
```

腳本使用 `sudo -n`，僅對 tcpdump 提權。預設埠 8080 必須可用，可透過 `MONITOR_CAPTURE_PORT` 調整。已完成的封包實測使用手動 tcpdump；詳見 [封包分析](docs/evidence/authorized-capture/packet_analysis.md)。

## 文件

- [系統設計](docs/design.md)
- [測試報告](docs/test_results.md)
- [功能與驗證對照](docs/requirements_matrix.md)
- [操作與展示指南](docs/demo_guide.md)
