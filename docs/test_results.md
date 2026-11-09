# 測試報告

測試日期：2026-11-08。環境為 WSL2 Ubuntu 26.04.1 LTS、x86_64，使用 GCC 15.2.0、Make 4.4.1、GDB 17.1 與 tcpdump 4.99.6。證據時間採 UTC。

## 驗證摘要

Clean build 與完整測試均成功，無編譯警告。重現指令：

```bash
python3 tests/verify.py
```

[完整紀錄](evidence/20261108T121625.359492Z-verification/transcript.txt) 包含建置、測試、動態連結與 GDB 檢查，並附 [結果 JSON](evidence/20261108T121625.359492Z-verification/result.json) 及 [原始碼 SHA-256](evidence/20261108T121625.359492Z-verification/source-sha256.json)。封包擷取另以手動 tcpdump 完成。

| 項目 | 實測結果 | 證據 |
|---|---|---|
| 基本功能與協定 | 四指令、選單、監控、Ctrl+C、分段／連續訊息及輸入邊界通過 | [功能測試](evidence/20261108T121625.694768Z-basic/transcript.txt) |
| 系統資訊 | CPU 雙取樣、記憶體算式與 /proc 比對、uptime 容差通過 | 同上 |
| 並行處理 | 十個獨立 Child，處理區間共同重疊 200 ms | [並行測試](evidence/20261108T121627.761408Z-concurrency/transcript.txt) |
| 日誌 | DEBUG=0／1 與兩端 runtime 0／1／2 組合通過 | [日誌測試](evidence/20261108T121634.086220Z-logging/transcript.txt) |
| 動態連結 | 兩端載入本地 libmonitor.so，RUNPATH 正確 | [完整紀錄](evidence/20261108T121625.359492Z-verification/transcript.txt) |
| 程序與資源管理 | 30 次連線後 Parent FD 維持 6；Child 全回收，同埠重啟成功 | [生命週期測試](evidence/20261108T121634.644792Z-lifecycle/transcript.txt) |
| 系統讀取失敗 | 三種 /proc 讀取失敗回 SYSTEM_INFO，其他查詢仍可使用 | [錯誤注入測試](evidence/20261108T121635.225997Z-proc-errors/transcript.txt) |
| 傳送完整性 | 1 MiB 資料驗證成功；阻塞寫入於 101 ms 逾時 | [I/O 測試](evidence/20261108T121636.163239Z-io/transcript.txt) |
| 封包擷取 | 46 包、五條連線、四筆查詢、10 FIN、0 RST；kernel drop 為 0 | [封包分析](evidence/authorized-capture/packet_analysis.md) |

## 並行結果

十條連線保持開啟後，以 barrier 同步發送 GET_CPU。Child PID 為 45572～45581，均屬於同一 Parent。

| 測量 | 結果 |
|---|---|
| 十筆並行查詢 | 0.228093 s |
| 十筆循序查詢 | 2.014794 s |
| 十個 Child 處理區間共同重疊 | 200 ms |

各 Client 起訖時間與 Child 事件見 [timings.json](evidence/20261108T121627.761408Z-concurrency/timings.json)。

## 防護比較

| 情境 | Baseline | Protected |
|---|---|---|
| 非法／超長輸入 | 誤接受帶額外內容的 GET_MEM 與 208-byte 長行 | 回 INVALID_REQUEST／REQUEST_TOO_LONG |
| 不傳送資料 | 觀察約 0.8 s 後 Child 仍等待 | 400 ms 期限後回 TIMEOUT 並回收 |
| 已關閉對端的傳送 | SIGPIPE 終止，exit=-13 | 回 EPIPE，probe 正常退出 |
| TCP reset | 記錄 ECONNRESET，Parent 繼續服務 | 記錄 ECONNRESET，Parent 繼續服務 |

另驗證部分字元不會重設完整 request 的期限。SIGPIPE 使用 socketpair 重現，TCP reset 為獨立整合測試；兩者結果分別記錄於 [防護測試](evidence/20261108T121630.788092Z-robustness/transcript.txt)。

## 封包與紀錄範圍

手動擷取取得完整握手、四種 request／response 及正常 FIN 關閉。GET_CPU 的 request→response 為 200.715 ms，GET_MEM 為 0.672 ms；數值僅代表本次執行。[原始 pcap](evidence/authorized-capture/monitor.pcap)、[應用輸出](evidence/authorized-capture/traffic.txt) 與 [擷取統計](evidence/authorized-capture/tcpdump-stats.txt) 均已保存。

自動擷取腳本已驗證語法與權限不足的失敗路徑，成功擷取採手動流程。早期受限環境的 BLOCKED 紀錄保留於 evidence；本報告引用後續具備相應權限的成功測試。系統讀取失敗透過獨立測試 binary 注入，不修改 /proc 或正式程式。
