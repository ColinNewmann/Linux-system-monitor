# Requirements Traceability Matrix

狀態依 2026-11-08 實際執行結果填寫。完整來源為 [test_results.md](test_results.md)，不把預定測試當成已通過。

| Requirement | 實作位置 | 驗證方式與證據 | 狀態 |
|---|---|---|---|
| R1：C TCP Client–Server | [client.c](../client/client.c)、[server.c](../server/server.c)、[protocol.c](../shared/protocol.c) | 真實 loopback TCP 四指令、互動模式、分段／連續訊息、錯誤回應；[basic](evidence/20261108T121625.694768Z-basic/transcript.txt) | PASS |
| R2：fork Process Model | [server.c](../server/server.c)：accept／fork、FD 關閉、self-pipe、waitpid、關機 | 十個獨立 Child 與 PPid、Child FD 檢查、回收與帶連線關機；[concurrency](evidence/20261108T121627.761408Z-concurrency/transcript.txt)、[lifecycle](evidence/20261108T121634.644792Z-lifecycle/transcript.txt) | PASS |
| R3：至少十人真正並行 | [test_concurrency.py](../tests/test_concurrency.py) | barrier 同時發送、十個 Child、處理區間共同重疊 200 ms；並行 0.228093 s／循序 2.014794 s；[timings.json](evidence/20261108T121627.761408Z-concurrency/timings.json) | PASS |
| R4：tcpdump 實際擷取與分析 | [capture.sh](../tests/capture.sh)、[展示指南](demo_guide.md) | 操作者啟動授權的 lo／8080 tcpdump；四種查詢、五條連線、46 包、10 FIN／0 RST，操作者回報 kernel drop=0；[pcap](evidence/authorized-capture/monitor.pcap)、[實際分析](evidence/authorized-capture/packet_analysis.md)、[統計](evidence/authorized-capture/tcpdump-stats.txt) | PASS |
| R5：Compile-time／Runtime Logging | [logging.h](../shared/logging.h)、[logging.c](../shared/logging.c)、[Makefile](../Makefile)、兩端啟動參數 | DEBUG=0／1，Server 各三層，每組 Client 各三層；ERROR 保留；[logging](evidence/20261108T121634.086220Z-logging/transcript.txt) | PASS |
| R6：真正共用 libmonitor.so | [shared/](../shared/)、[Makefile](../Makefile) | 兩端直接呼叫收送及日誌；ldd 指向本地 .so、DT_NEEDED／RUNPATH 正確並實際執行；[總紀錄](evidence/20261108T121625.359492Z-verification/transcript.txt) | PASS |
| R7：四種系統資訊 | [system_info.c](../server/system_info.c)、[system_info.h](../server/system_info.h) | CPU 雙取樣、memory 定義及 /proc 比對、uptime；[basic](evidence/20261108T121625.694768Z-basic/transcript.txt)。讀取失敗回應另由 [proc-errors](evidence/20261108T121635.225997Z-proc-errors/transcript.txt) 驗證 | PASS |
| R8：三組安全防護比較 | [protocol.c](../shared/protocol.c)、[server.c](../server/server.c)、[system_info.c](../server/system_info.c)、[test_robustness.py](../tests/test_robustness.py)、[disconnect_probe.c](../tests/disconnect_probe.c) | A 非法／超長誤接受對照；B silent／slow-partial timeout；C SIGPIPE／EPIPE 與 TCP reset 後服務；[robustness](evidence/20261108T121630.788092Z-robustness/transcript.txt)。兩版本分開建置，無未定義行為 | PASS |
| R9：GNU Make | [Makefile](../Makefile) | make、clean、test、test-concurrent、test-robustness 與額外 targets 均實際執行；[clean build 與全測試](evidence/20261108T121625.359492Z-verification/transcript.txt) | PASS |

## 其他交付條件

| 項目 | 實作／文件 | 實測狀態 |
|---|---|---|
| Terminal 互動選單 | [client.c](../client/client.c) | basic 測試完成 1～4 查詢與 6 離開 |
| 可設定間隔的連續監控 | Client `--monitor --interval-ms --count` | 三次更新及 Ctrl+C 正常退出 |
| Partial read／write | shared protocol；[io_probe.c](../tests/io_probe.c) | 分段 request、pipelining；1 MiB 小 buffer 完整傳送 |
| 無明顯 FD／Child 殘留 | Parent FD／PID 管理 | 30 次連線 FD 6→6，Child 全回收；十連線關機後 Child PID 消失 |
| 正常結束與重新啟動 | Server 訊號處理、SO_REUSEADDR | 同埠重啟成功 |
| GDB 可除錯 | GCC `-g` | Client main breakpoint、bt、正常結束已實測 |
| README／系統設計／Demo | [README](../README.md)、[design](design.md)、[demo](demo_guide.md) | 已交付；含實際測試證據、手動擷取流程與權限限制說明 |
| 原始證據可追溯 | [evidence/](evidence/) | transcript、PID 日誌、JSON、source SHA-256 已保存 |

R1～R9 均已有實際通過證據。R4 的成功證據來自操作者啟動的手動 tcpdump；不將自動 capture.sh 尚未驗證的成功路徑算入實測。未執行 commit、push、套件安裝或系統設定修改。工作區的 `.git` 目前不是有效 Git repository，本次未初始化或修改它。
