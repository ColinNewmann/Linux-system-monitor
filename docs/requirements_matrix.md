# 功能與驗證對照

實測日期：2026-11-08。詳細數據見 [測試報告](test_results.md)。

| 功能 | 主要實作 | 驗證方式與證據 |
|---|---|---|
| TCP 查詢與連續監控 | [Client](../client/client.c)、[Protocol](../shared/protocol.c) | 四指令、互動選單、監控、分段訊息與錯誤回應；[功能測試](evidence/20261108T121625.694768Z-basic/transcript.txt) |
| 多程序與並行連線 | [Server](../server/server.c) | 十個 Child、PPid 與處理區間重疊；[並行測試](evidence/20261108T121627.761408Z-concurrency/timings.json) |
| 程序回收與資源管理 | [Server](../server/server.c) | FD 穩定、Child 回收、關機及重啟；[生命週期測試](evidence/20261108T121634.644792Z-lifecycle/transcript.txt) |
| 系統資訊 | [System Info](../server/system_info.c) | /proc 數據比對與讀取失敗回應；[功能測試](evidence/20261108T121625.694768Z-basic/transcript.txt)、[錯誤注入](evidence/20261108T121635.225997Z-proc-errors/transcript.txt) |
| 共用函式庫與建置 | [Shared](../shared/)、[Makefile](../Makefile) | Clean build、ldd、DT_NEEDED 與 RUNPATH；[完整驗證](evidence/20261108T121625.359492Z-verification/transcript.txt) |
| 雙層日誌控制 | [Logging](../shared/logging.h)、[Makefile](../Makefile) | 編譯設定與兩端 runtime 層級組合；[日誌測試](evidence/20261108T121634.086220Z-logging/transcript.txt) |
| 輸入、逾時與斷線防護 | [Protocol](../shared/protocol.c)、[Server](../server/server.c) | Baseline／Protected 比較與大量資料傳送；[防護測試](evidence/20261108T121630.788092Z-robustness/transcript.txt)、[I/O 測試](evidence/20261108T121636.163239Z-io/transcript.txt) |
| TCP 封包分析 | tcpdump、[分析腳本](evidence/authorized-capture/analyze.py) | 46 包、完整握手、查詢回應與 FIN；[分析報告](evidence/authorized-capture/packet_analysis.md) |

上述功能均已通過所列測試。測試命令、原始輸出與環境資訊保存在各證據目錄。
