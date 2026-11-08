# 實際測試結果

執行日期：2026-11-08（Asia/Taipei）。證據目錄與程式日誌使用 UTC；例如 `12:16:25 UTC` 對應台灣 `20:16:25`。

## 驗證環境與結果

- 實際路徑：`/home/p1yan/mid-project`。
- WSL2 Ubuntu 26.04.1 LTS，kernel `6.18.54.1-microsoft-standard-WSL2`，x86_64。
- GCC 15.2.0、GNU Make 4.4.1、GDB 17.1、tcpdump 4.99.6。
- 預設代理沙箱不允許 socket：最初測試記錄為 BLOCKED。取得執行權限審核後，在同一 WSL 的允許 socket 環境執行以下通過的真實測試，沒有使用模擬輸出替代 TCP。
- 截至本文件整理時：**R1～R9 全部已有實際驗證證據。** R4 由操作者在自己的 WSL 終端機啟動授權的 tcpdump，代理產生並驗證四種查詢流量，完成後由操作者停止擷取並回報統計。

完整重現指令：

```bash
python3 tests/verify.py
```

[完整驗證 transcript](evidence/20261108T121625.359492Z-verification/transcript.txt) 包含 clean build、完整測試、ldd／readelf 與 GDB。結果為 [PASS](evidence/20261108T121625.359492Z-verification/result.json)，附有 [原始碼 SHA-256](evidence/20261108T121625.359492Z-verification/source-sha256.json)。`make clean`、`make -j2`、`make test-all` 均 exit 0；這次所有編譯設定沒有 compiler warning。

## 各項實測

| 測試與命令 | 本次實際結果 | 原始證據 |
|---|---|---|
| `make test` | 四指令、互動選單、三次連續監控、Ctrl+C、非法參數通過 | [basic](evidence/20261108T121625.694768Z-basic/transcript.txt) |
| 分段／連續訊息 | 分段 GET_MEM、同連線兩筆查詢均正確；不完整 EOF、NUL／CR／控制字元拒絕 | 同上 |
| 長度邊界 | 127 字元加 LF 進入指令驗證；128 字元加 LF 回 REQUEST_TOO_LONG | 同上 |
| `/proc` 數值比對 | MemTotal 相等，used 算式與百分比正確；CPU 0～100 且雙取樣，uptime 在容差內 | 同上 |
| `make test-concurrent` | 同時十個 Child；Client 區間與 Child 處理區間重疊；退出後回收完成 | [concurrency](evidence/20261108T121627.761408Z-concurrency/transcript.txt)、[timings](evidence/20261108T121627.761408Z-concurrency/timings.json) |
| `make test-robustness` | A／B／C 兩版對照與 TCP reset 後服務均通過 | [robustness](evidence/20261108T121630.788092Z-robustness/transcript.txt) |
| `make test-logging` | DEBUG=0／1 × Server level 0／1／2；每組再比較 Client level 0／1／2 | [logging](evidence/20261108T121634.086220Z-logging/transcript.txt) |
| `make test-linkage` | 兩端皆載入工作區 libmonitor.so，DT_NEEDED 與相對 RUNPATH 正確 | [完整驗證 transcript](evidence/20261108T121625.359492Z-verification/transcript.txt) |
| `make test-lifecycle` | 30 次連線後 Parent FD 6→6、Child 全回收；十個活動連線時關機與同埠重啟通過 | [lifecycle](evidence/20261108T121634.644792Z-lifecycle/transcript.txt) |
| `make test-proc-errors` | 三個 /proc fopen 失敗分別回 SYSTEM_INFO；同連線仍可查詢未受影響的資料 | [proc-errors](evidence/20261108T121635.225997Z-proc-errors/transcript.txt) |
| `make test-io` | 1,048,576 bytes 經小 buffer 完整傳送並逐 byte 驗證；阻塞寫入 101 ms 後逾時 | [io](evidence/20261108T121636.163239Z-io/transcript.txt) |
| GDB smoke check | 中斷於 Client main、bt 顯示 main，continue 後正常退出 | [完整驗證 transcript](evidence/20261108T121625.359492Z-verification/transcript.txt) |
| 實際 tcpdump 擷取與離線分析 | 46 包、五條連線、四種 request／response、10 FIN、0 RST；操作者回報 kernel drop=0 | [pcap](evidence/authorized-capture/monitor.pcap)、[封包分析](evidence/authorized-capture/packet_analysis.md)、[終端機統計](evidence/authorized-capture/tcpdump-stats.txt) |

`/proc` 故障測試是明確標示的測試 binary 故障注入；一般 TCP 查詢使用未注入的正式 Server。socketpair 測試使用真正 Linux socket，作用是確定性驗證共用傳送函式，不用它取代 TCP 整合測試。

## 十人並行的證據

最後一輪觀察到十個 Child PID：45572～45581，逐一確認 PPid 為同一 Server Parent，且不是 zombie。十條連線保持開啟後，測試端以 barrier 同時發送 GET_CPU。

```text
concurrent_s=0.228093
sequential_s=2.014794
server_overlap_ms=200
```

上述數字由 `timings.json` 的原始數值四捨五入而得。每個 Child 的 request_start／request_end 記錄在該目錄的 Server log；十個區間共有 200 ms 重疊。除了總耗時較短，也有獨立 PID 與實際處理區間的證據。

## 三組 Robustness 對照

### A：非法／超長 request

執行 `make test-robustness` 後，測試傳送 `GET_MEM garbage\n` 及 208-byte 長行。

- Baseline：兩者均錯誤回傳 `OK MEM ...`。
- Protected：分別回傳 `ERR INVALID_REQUEST`、`ERR REQUEST_TOO_LONG`。
- Baseline 透過有界字串與前綴解析示範錯誤，不使用 buffer overflow。

### B：Client 不傳送資料

測試設定 Protected 的 request deadline 為 400 ms。

```text
baseline:  observed_s=0.801 remaining_children=[45661]
protected: observed_s=0.800 remaining_children=[]
```

Protected 對端讀到 `ERR TIMEOUT` 與 EOF；Baseline 在觀察期間持續等待，之後由測試關閉連線清理。此實測證明固定觀察期間的行為差異；Baseline 的無期限設定另可由程式中的 `read_timeout = -1` 檢查。額外 slow-partial 測試確認送出部分字元不重設完整行期限。

### C：突然斷線／SIGPIPE

使用兩版 libmonitor.so 的 disconnect_probe：

```text
baseline:  exit=-13
protected: result=-1 errno=32 EPIPE=32
           exit=0
```

Baseline 確實被 SIGPIPE 終止；Protected 留在正常錯誤處理流程。真實 TCP reset 測試則在兩版均觀察到 `errno=104`（Connection reset by peer），Child 結束、Parent 繼續接受下一筆 GET_MEM。**沒有宣稱這次 TCP reset 導致 Baseline Server 被 SIGPIPE 終止。**

## 已解決的開發問題

- 初始環境變數 `DEBUG=release` 干擾 Make 預設值，已改用專案預設與命令列覆寫。
- 初次 DEBUG=0 編譯曾有僅供日誌使用的變數未使用警告，已以前處理條件處理。
- 增加有上限的 poll 等待，避免停止訊號與旗標檢查間的極小競態造成延遲。
- 最後一輪 clean build 與全部無 sudo 測試是在這些修正後執行。

## R4：實際 packet capture（已完成）

最初遇到權限限制，後續由操作者在自己的 WSL 終端機完成 sudo 驗證並啟動擷取，於 2026-11-08 20:26（Asia/Taipei）取得真實封包。

```bash
sudo tcpdump -Z p1yan -i lo -nn -s 0 -U \
  -w /home/p1yan/mid-project/docs/evidence/authorized-capture/monitor.pcap \
  'tcp port 8080'
```

代理依序執行 GET_STATUS、GET_CPU、GET_MEM、GET_UPTIME，回應均成功，Server 正常停止。操作者之後提供停止擷取的最終統計：

```text
46 packets captured
92 packets received by filter
0 packets dropped by kernel
```

離線重現：

```bash
TZ=UTC tcpdump -nn -tttt -A -r docs/evidence/authorized-capture/monitor.pcap
python3 docs/evidence/authorized-capture/analyze.py
```

實際分析取得 46 筆完整 pcap record、五組 SYN／SYN-ACK、十個 FIN、零個 RST。五條連線包含一條 readiness probe，以及四條各傳送一筆 request／response 的查詢連線。GET_CPU 的 request→response 時差為 200.715 ms，與 CPU 雙取樣設計相符；GET_MEM 為 0.672 ms。這些是本次封包時間，並非效能保證。

詳細的握手、相對 sequence／ACK、LF hex 與連線關閉分析見 [packet_analysis.md](evidence/authorized-capture/packet_analysis.md)。[traffic.txt](evidence/authorized-capture/traffic.txt) 保存應用端指令與輸出；[analysis.json](evidence/authorized-capture/analysis.json) 保存計數與 pcap SHA-256。**R4：PASS。**

### 先前權限限制的原始紀錄

一般帳號的實際嘗試：

```bash
make capture
```

tcpdump 回報：

```text
tcpdump: lo: You don't have permission to perform this capture on that device
(Attempt to create packet socket failed - CAP_NET_RAW may be required)
```

[原始 tcpdump 錯誤](evidence/20261108T121233-44668-capture/tcpdump.log) 與 [執行紀錄](evidence/20261108T121233-44668-capture/transcript.txt) 已保留。

使用者明確授權這次 sudo tcpdump 後，執行：

```bash
MONITOR_CAPTURE_SUDO=1 make capture
```

實際結果：

```text
sudo: interactive authentication is required
```

見 [sudo 失敗紀錄](evidence/20261108T121725-46174-capture/tcpdump.log) 與 [執行紀錄](evidence/20261108T121725-46174-capture/transcript.txt)。這是本機驗證限制，不是使用者未授權，也不是自動審核拒絕。

上述失敗紀錄保留作為環境限制的歷史證據，已由本節前面的成功手動擷取補足 R4。`capture.sh` 的語法與失敗路徑已實測；本次成功使用的是明確列出的手動 tcpdump 指令，不將它誤稱為自動 capture.sh 成功執行。

最初受限沙箱的 [BLOCKED basic 紀錄](evidence/20261108T121022.650675Z-basic/result.json) 也保留，未刪除或改成 PASS。後續已通過的 socket 測試是另一次允許執行環境的實測。
