# R4：Loopback TCP 封包分析

實測時間：2026-11-08 20:26:56～20:26:57（Asia/Taipei）；對應 12:26:56～12:26:57 UTC。下列封包時間使用 UTC。

## 擷取與分析指令

操作者在自己的 WSL 終端機完成 sudo 驗證並啟動：

```bash
sudo tcpdump -Z p1yan -i lo -nn -s 0 -U \
  -w /home/p1yan/mid-project/docs/evidence/authorized-capture/monitor.pcap \
  'tcp port 8080'
```

收到操作者「已啟動」通知後，代理以一般帳號啟動 Server，分別執行 GET_STATUS、GET_CPU、GET_MEM、GET_UPTIME，最後正常停止 Server。實際指令、輸出、PID 與退出碼見 [traffic.txt](traffic.txt) 及 [Server 日誌](server-8080-13161166959003.log)。

離線重現分析，從專案根目錄執行：

```bash
TZ=UTC tcpdump -nn -tttt -A -r docs/evidence/authorized-capture/monitor.pcap
TZ=UTC tcpdump -nn -tttt -X -r docs/evidence/authorized-capture/monitor.pcap
python3 docs/evidence/authorized-capture/analyze.py
```

分析腳本僅用 Python 標準函式庫，針對這次實際取得的 classic pcap／Ethernet／IPv4 格式驗證：完整 packet records、握手、ACK、LF、四個 request／response 及正常 FIN。它不宣稱支援任意 pcap 格式或一般 TCP stream reassembly。

## 實際擷取內容

| 項目 | pcap 實際結果 |
|---|---|
| 檔案大小 | 4,183 bytes |
| 格式 | classic pcap，EN10MB，snapshot length 262144 |
| 封包數 | 46 |
| TCP 連線 | 5，全部為 127.0.0.1 與 Server 8080 |
| SYN／SYN-ACK | 各 5 |
| FIN | 10，每條連線雙方各一個 |
| RST | 0 |
| 帶 payload 封包 | 8：4 個 request、4 個 response |

五條連線中的 Client port 40930 是啟動 readiness probe，只完成握手與關閉，不送查詢，因此有六個封包。其餘四條各十個封包，合計 `6 + 4 × 10 = 46`。不是五次系統資訊查詢。

所有 pcap record 均完整保存，沒有被 snapshot length 截斷。操作者停止 tcpdump 後提供的實際終端機統計如下，原文保存在 [tcpdump-stats.txt](tcpdump-stats.txt)：

```text
46 packets captured
92 packets received by filter
0 packets dropped by kernel
```

46 captured 與 pcap 的 46 筆 record 相符。received by filter 是作業系統擷取統計，不能當成 pcap 中的獨立封包數；不將 92 解讀為有 92 個 TCP segment 寫入檔案。kernel drop 為操作者回報的 0，不是從 pcap 推測。

## 四種 Request／Response

封包編號依檔案順序從 1 開始。byte 數包含 LF，時間差由 request payload 與 response payload 的擷取時間相減。

| Client port | 指令 | Request／Response packet | Request／Response bytes | Request→Response |
|---|---|---|---|---|
| 40932 | GET_STATUS | 10／12 | 11／130 | 200.821 ms |
| 40942 | GET_CPU | 20／22 | 8／36 | 200.715 ms |
| 40954 | GET_MEM | 30／32 | 8／76 | 0.672 ms |
| 40962 | GET_UPTIME | 40／42 | 11／27 | 0.396 ms |

GET_STATUS 與 GET_CPU 均包含約 200 ms CPU 雙取樣，封包時間差與應用回應中的 sample_ms 相符。這是這次測試的觀察，不是所有負載下的延遲保證。

## GET_MEM 的完整流程

這條連線是 `127.0.0.1:40954 ↔ 127.0.0.1:8080`，對應封包 27～36：

1. 27：Client SYN。
2. 28：Server SYN-ACK，ACK 為 Client 初始 sequence + 1。
3. 29：Client ACK，三向交握完成。
4. 30（12:26:57.369096 UTC）：Client 傳送 `GET_MEM\n`，8 bytes；相對 sequence `1:9`。
5. 31：Server ACK 9，確認八個 payload bytes。
6. 32（12:26:57.369768 UTC）：Server 傳送以下 76-byte response，相對 sequence `1:77`：

```text
OK MEM total_kb=8019648 available_kb=6326924 used_kb=1692724 used_pct=21.11
```

7. 33：Client ACK 77。
8. 34：Client FIN-ACK，相對 sequence 9。
9. 35：Server FIN-ACK，ACK 10；同時確認 Client 的 FIN。
10. 36：Client ACK 78，確認 Server 的 FIN，正常結束。

此次 ACK 與 FIN 合併，因此關閉階段是三個 segment，不應硬套為必定四個封包。SYN、FIN 各消耗一個 sequence number。

Request 的實際 payload hex 為 `47 45 54 5f 4d 45 4d 0a`，最後 `0a` 就是訊息邊界。Response 同樣以 `0a` 結束。詳見 [packets-hex.txt](packets-hex.txt) 與 [packet-details.json](packet-details.json)。

## 解讀注意事項

- tcpdump 可能因為 8080 的預設 port heuristic 顯示 `HTTP`；本程式的實際 payload 是自訂換行協定，沒有 HTTP method、header 或 status line。
- 這次每筆 request／response 恰好各在一個 TCP segment 中，不代表 TCP 保證一個 send 對應一個 recv；分段與連續訊息已由獨立功能測試驗證。
- 分析中的「46」來自實際 pcap record 計數，不是推估的 tcpdump 終端機統計。
- sudo 只用於操作者的 tcpdump。Server、Client 與離線分析均未提權；沒有安裝套件或修改系統設定。

原始檔：[monitor.pcap](monitor.pcap)；文字解碼：[packets.txt](packets.txt)；結構化摘要與 SHA-256：[analysis.json](analysis.json)。

## 擷取停止狀態

代理已正常停止自己啟動的 Server；操作者在收到 Ctrl+C 停止要求後回覆了上述 tcpdump 最終統計。停止後重新解碼與分析 pcap，確認四筆查詢、完整握手與正常 FIN 關閉。**R4：PASS。**
