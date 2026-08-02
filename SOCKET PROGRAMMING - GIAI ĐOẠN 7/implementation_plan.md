# Giai đoạn 7 — Congestion Control + Data Integrity (GIAI ĐOẠN CUỐI)

## 1. PHÂN TÍCH TRẠNG THÁI HIỆN TẠI (SAU GĐ6)

### 1.1 Tầng RDT hiện tại

| Thành phần | Trạng thái |
|------------|------------|
| RdtPacket (struct + serialize/deserialize + checksum) | ✅ Hoàn chỉnh |
| Stop-and-Wait ARQ (`rdtSend` / `rdtReceive`) | ✅ Hoạt động — nhưng **chậm** vì chỉ gửi 1 gói rồi chờ ACK |
| Packet Loss Simulation | ✅ Có sẵn |
| DataChannel interface public | ✅ Giữ nguyên từ GĐ5 |

### 1.2 Lệnh HASH trong project

| Vị trí | Trạng thái |
|--------|------------|
| `FtpCommand::HASH` trong `lib.h` enum (dòng 68) | ✅ Đã khai báo |
| `toFtpCommand("HASH")` trong `CmdHandler.cpp` (dòng 43) | ✅ Đã map |
| `handleHash()` method | ❌ **Chưa có** — hiện trả "502 Command not implemented" |
| HASH trong HELP listing | ❌ **Chưa liệt kê** |
| HASH trong HELP detail switch | ❌ **Chưa có case** |

### 1.3 Vấn đề cần giải quyết GĐ7

```
GĐ7 gồm 4 việc:
  ┌─ 1. Go-Back-N Sliding Window   (thay Stop-and-Wait)
  ├─ 2. Congestion Window (cwnd)    (slow-start + multiplicative decrease)
  ├─ 3. HASH <filename>             (server tính SHA-256, trả qua TCP)
  └─ 4. LHASH <filename>            (client hash cục bộ, không gửi server)
```

---

## 2. THIẾT KẾ CHI TIẾT

### 2.1 Go-Back-N Sliding Window

**So sánh Stop-and-Wait vs Go-Back-N:**

| | Stop-and-Wait (GĐ6) | Go-Back-N (GĐ7) |
|-|---------------------|-----------------|
| Số gói gửi cùng lúc | 1 | Tối đa `cwnd` gói |
| SeqNum | 0 hoặc 1 (xoay vòng) | 0, 1, 2, 3, ... (tăng dần) |
| Khi timeout | Gửi lại 1 gói | Gửi lại **tất cả** gói từ `base` đến `nextSeqNum-1` |
| ACK | Từng gói riêng | **Cumulative**: ACK(n) = "đã nhận đúng đến gói n" |
| Throughput | Thấp (1 packet / RTT) | Cao (window / RTT) |

#### Sender (rdtSend — Go-Back-N)

```
Chuẩn bị:
    Chia data thành N chunk → tạo N gói DATA (seq=0..N-1) + 1 gói FIN (seq=N)
    totalPackets = N + 1
    base = 0, nextSeqNum = 0
    cwnd = 1, ssthresh = GBN_INITIAL_SSTHRESH

Vòng lặp chính (while base < totalPackets):
    ┌─ PHASE 1: GỬI — gửi tất cả gói trong cửa sổ [nextSeqNum, base+cwnd)
    │   while nextSeqNum < min(base + cwnd, totalPackets):
    │       sendto(packet[nextSeqNum])
    │       nextSeqNum++
    │
    ├─ PHASE 2: CHỜ ACK (recvfrom với timeout)
    │   ├─ TIMEOUT:
    │   │     ssthresh = max(cwnd/2, 1)    ← multiplicative decrease
    │   │     cwnd = 1                      ← reset window
    │   │     nextSeqNum = base             ← GO BACK: gửi lại từ base
    │   │     retryCount++
    │   │     if retryCount > MAX_RETRIES: FAIL
    │   │
    │   ├─ ACK hợp lệ (seqNum >= base):
    │   │     ackedCount = ack.seqNum - base + 1
    │   │     base = ack.seqNum + 1          ← trượt cửa sổ
    │   │     retryCount = 0
    │   │     CONGESTION CONTROL:
    │   │       if cwnd < ssthresh:
    │   │           cwnd += ackedCount       ← slow-start (tăng nhanh)
    │   │       else:
    │   │           cwnd += 1                ← congestion avoidance (tăng chậm)
    │   │       cwnd = min(cwnd, GBN_MAX_WINDOW)
    │   │
    │   └─ ACK lỗi/sai seq → bỏ qua, chờ tiếp
    └─

Khi base == totalPackets: DONE (bao gồm cả FIN đã ACK)
```

#### Receiver (rdtReceive — Go-Back-N)

```
expectedSeq = 0

Vòng lặp (while true):
    recvfrom()
    ├─ Checksum sai → DROP, KHÔNG ACK
    │
    ├─ seqNum == expectedSeq:
    │     ├─ FLAG_FIN → gửi ACK(seqNum) → BREAK
    │     └─ FLAG_DATA → deliver → gửi ACK(seqNum) → expectedSeq++
    │
    └─ seqNum != expectedSeq (out of order):
          ├─ expectedSeq > 0 → gửi ACK(expectedSeq - 1)  ← cumulative ACK cho gói cuối đúng
          └─ expectedSeq == 0 → DROP im lặng (chưa nhận gói nào)
```

### 2.2 Congestion Control (cwnd)

```
┌────────────────────────────────────────────────────────────┐
│ cwnd                                                       │
│  32 ┤                              ╱──── cap (MAX_WINDOW)  │
│     │                             ╱                        │
│  16 ┤- - - - - ssthresh - - - - -╱- - - - - - - - - - - - │
│     │              ╱  ╱ ╱ ╱ ╱ ╱╱                          │
│   8 ┤             ╱                                        │
│   4 ┤           ╱    ← slow-start (exponential)            │
│   2 ┤         ╱                                            │
│   1 ┤───────╱          ← congestion avoidance (linear)     │
│     └──────┬──────┬──────┬──────┬──────┬──────── time      │
│          start          timeout→cwnd=1                     │
│                         ssthresh=cwnd/2                    │
└────────────────────────────────────────────────────────────┘
```

**Hằng số mới trong RdtPacket.h:**
```cpp
constexpr int GBN_MAX_WINDOW      = 32;  // Kích thước cửa sổ tối đa
constexpr int GBN_INITIAL_SSTHRESH = 16;  // Ngưỡng slow-start ban đầu
```

### 2.3 HASH Command (Server)

**Luồng:**
1. Client gửi `HASH test.txt` qua TCP
2. Server nhận → `handleHash(session, "test.txt")`
3. Resolve path → đọc file → tính SHA-256 bằng **BCrypt API** (Windows CNG)
4. Trả về `"213 SHA-256 <64_hex_chars>\r\n"` qua TCP

**BCrypt API flow:**
```
BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM)
  → BCryptCreateHash(hAlg, &hHash)
    → BCryptHashData(hHash, fileData, fileSize)
      → BCryptFinishHash(hHash, hashOutput, 32)
        → Convert 32 bytes → 64 hex chars
BCryptDestroyHash(hHash)
BCryptCloseAlgorithmProvider(hAlg)
```

> [!NOTE]
> BCrypt là **API hệ điều hành Windows** (CNG — Cryptography: Next Generation), KHÔNG phải thư viện bên thứ 3. Chỉ cần `#include <bcrypt.h>` + link `bcrypt.lib`. Hoàn toàn hợp lệ theo quy tắc project.

### 2.4 LHASH Command (Client)

**Luồng:**
1. User gõ `LHASH test.txt` ở client
2. Client **KHÔNG gửi lệnh này tới server** — xử lý hoàn toàn cục bộ
3. Đọc file `test.txt` tại máy client → tính SHA-256 → hiển thị
4. User tự so sánh thủ công với HASH (server) để kiểm tra data integrity

**Vị trí xử lý:** Trong `ControlChannel::run()` (vòng lặp bàn phím), kiểm tra "LHASH" **trước** khi parse lệnh 4 ký tự chuẩn → nếu match thì hash local + `continue` (không send TCP).

---

## 3. DANH SÁCH FILE CẦN THAO TÁC

### SERVER

| # | File | Hành động | Nội dung thay đổi |
|---|------|-----------|-------------------|
| 1 | `HashUtil.h` | **TẠO MỚI** | Khai báo `computeFileSHA256()` |
| 2 | `HashUtil.cpp` | **TẠO MỚI** | Implement SHA-256 bằng BCrypt API |
| 3 | `RdtPacket.h` | **SỬA** | Thêm `GBN_MAX_WINDOW`, `GBN_INITIAL_SSTHRESH`, cập nhật comment seqNum |
| 4 | `DataChannel.cpp` | **SỬA** | Viết lại `rdtSend` / `rdtReceive` cho Go-Back-N + congestion control |
| 5 | `CmdHandler.h` | **SỬA** | Thêm `handleHash()` declaration |
| 6 | `CmdHandler.cpp` | **SỬA** | Thêm `handleHash()`, case HASH trong switch, HASH trong HELP |
| 7 | `SERVER.vcxproj` | **SỬA** | Thêm HashUtil.h/.cpp |

### CLIENT

| # | File | Hành động | Nội dung thay đổi |
|---|------|-----------|-------------------|
| 8 | `HashUtil.h` | **TẠO MỚI** | Copy từ Server |
| 9 | `HashUtil.cpp` | **TẠO MỚI** | Copy từ Server |
| 10 | `RdtPacket.h` | **SỬA** | Copy từ Server (thêm hằng số GBN) |
| 11 | `DataChannel.cpp` | **SỬA** | Copy từ Server (Go-Back-N) |
| 12 | `ControlChannel.cpp` | **SỬA** | Thêm `#include "HashUtil.h"` + xử lý LHASH trong vòng lặp bàn phím |
| 13 | `CLIENT.vcxproj` | **SỬA** | Thêm HashUtil.h/.cpp |

### File KHÔNG SỬA (xác nhận giữ nguyên 100%)

| File | Lý do |
|------|-------|
| `lib.h` (cả 2) | `FtpCommand::HASH` đã có sẵn, không cần thêm |
| `DataChannel.h` (cả 2) | Signature `rdtSend` / `rdtReceive` không đổi |
| `RdtPacket.cpp` (cả 2) | Serialize/deserialize/checksum không đổi — seqNum vẫn uint32_t |
| `Session.h/.cpp` | Không liên quan |
| `ControlChannel.h` (Client) | Không thêm method public mới |
| `ControlChannel.h/.cpp` (Server) | Kênh TCP, không liên quan |
| `Server.cpp` / `Client.cpp` | main() không thay đổi |

---

## 4. Open Questions

> [!IMPORTANT]
> **Q1:** Giá trị mặc định cho hằng số Go-Back-N:
> - `GBN_MAX_WINDOW = 32` (tối đa 32 gói trong cửa sổ)
> - `GBN_INITIAL_SSTHRESH = 16` (ngưỡng chuyển slow-start → congestion avoidance)
> 
> Bạn muốn chỉnh giá trị nào không?

> [!IMPORTANT]
> **Q2:** Lệnh LHASH ở client — bạn muốn cú pháp `LHASH <filename>` (hash file tại thư mục hiện tại của client) đúng không? Hay cần đường dẫn tuyệt đối?

---

## 5. Verification Plan

### Build
- Compile cả SERVER và CLIENT — 0 errors, 0 new warnings.

### Go-Back-N + Congestion Control
1. Bật `SIMULATE_PACKET_LOSS = true`, `LOSS_PERCENT = 10`
2. STOR file lớn → quan sát log:
   - `[GBN] cwnd=1 → 2 → 4 → 8 → 16` (slow-start)
   - `[GBN] Timeout! ssthresh=8, cwnd=1` (multiplicative decrease)
   - `[GBN] Retransmit from base=...` (go-back)
3. Transfer vẫn thành công dù có mất gói

### HASH / LHASH
1. STOR `test.txt` lên server
2. Server: `HASH test.txt` → trả SHA-256
3. Client: `LHASH test.txt` → tính SHA-256 cục bộ
4. So sánh 2 hash → phải **giống hệt nhau**

### Functional Regression
- STOR / RETR / STOU / APPE / LIST / NLST / ABOR — tất cả vẫn hoạt động như GĐ6
