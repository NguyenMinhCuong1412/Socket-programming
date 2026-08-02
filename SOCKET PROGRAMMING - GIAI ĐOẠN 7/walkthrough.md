# Walkthrough — Giai đoạn 7: Congestion Control + Data Integrity

## Tổng quan thay đổi

Nâng cấp tầng **UDP Reliable** từ **Stop-and-Wait ARQ** lên **Go-Back-N Sliding Window** kết hợp **Congestion Control** (`cwnd`, Slow-Start, Multiplicative Decrease), bổ sung tính năng kiểm tra toàn vẹn dữ liệu SHA-256 bằng lệnh `HASH` (Server) và `LHASH` (Client).

---

## File đã tạo mới / sửa

### 1. HashUtil.h + HashUtil.cpp (Cả SERVER và CLIENT)
- Dùng Windows BCrypt API (`bcrypt.h`, `bcrypt.lib` - CNG native Windows API) để tính checksum SHA-256 của file.
- Trả về chuỗi hex 64 ký tự (lowercase).

### 2. RdtPacket.h (Cả SERVER và CLIENT)
- Bổ sung hằng số cấu hình Go-Back-N & Congestion Control:
  - `GBN_MAX_WINDOW = 32`
  - `GBN_INITIAL_SSTHRESH = 16`
- Sequence number dạng tăng dần 32-bit (`0, 1, 2, ...`).

### 3. DataChannel.cpp (Cả SERVER và CLIENT)
- **`rdtSend()` (Go-Back-N Sender + Congestion Control)**:
  - Chia file thành các gói `seq = 0..N-1` (DATA) và `N` (FIN).
  - Truyền cửa sổ trượt `base .. nextSeqNum < base + cwnd`.
  - Cumulative ACK: Trượt `base = ack.seqNum + 1`.
  - **Slow Start** (`cwnd < ssthresh`): `cwnd += ackedCount`.
  - **Congestion Avoidance** (`cwnd >= ssthresh`): `cwnd += 1` per batch.
  - **Multiplicative Decrease (Timeout)**: `ssthresh = max(cwnd / 2, 1)`, `cwnd = 1`, `nextSeqNum = base` (quay lại gửi từ base).
- **`rdtReceive()` (Go-Back-N Receiver)**:
  - Nhận theo `expectedSeq`.
  - Gói đúng thứ tự (`seqNum == expectedSeq`): nhận payload, `expectedSeq++`, gửi ACK.
  - Gói sai thứ tự / trùng lặp: bỏ payload, gửi lại Cumulative ACK cho gói đúng gần nhất (`expectedSeq - 1`).

### 4. CmdHandler.h + CmdHandler.cpp (SERVER)
- Thêm method `handleHash(Session&, const string&)`:
  - Gọi `computeFileSHA256()` tính SHA-256 của file trong `SERVER_ROOT`.
  - Trả về phản hồi dạng `213 SHA-256 <64_hex_chars>`.
- Bổ sung `case FtpCommand::HASH` trong dispatch và trợ giúp `HELP`.

### 5. ControlChannel.cpp (CLIENT)
- Xử lý lệnh `LHASH <filename>` ngay tại vòng lặp bàn phím Client:
  - Tính SHA-256 của tệp tin cục bộ phía Client và in ra màn hình.
  - Không gửi lệnh `LHASH` lên Server.

### 6. SERVER.vcxproj + CLIENT.vcxproj
- Tích hợp `HashUtil.h` và `HashUtil.cpp` vào project Visual Studio.

---

## Luồng hoạt động Go-Back-N + Congestion Control

```
SENDER (rdtSend)                                RECEIVER (rdtReceive)
──────────────                                  ────────────────────
base=0, nextSeqNum=0, cwnd=1, ssthresh=16       expectedSeq=0

[Window: pkt 0]
send(0) ──►                                     recv(0) -> Expected! Accept -> ACK(0)
        ◄── ACK(0)
cwnd = 2 (Slow Start)

[Window: pkt 1, 2]
send(1), send(2) ──►                            recv(1) -> Accept -> ACK(1)
                                                recv(2) -> Accept -> ACK(2)
        ◄── ACK(1), ACK(2)
cwnd = 4 (Slow Start)

... Nếutimeout xảy ra ...
ssthresh = max(cwnd/2, 1)
cwnd = 1
nextSeqNum = base (Go-Back-N Resend)
```

---

## Hướng dẫn test và so sánh SHA-256

### 1. Test HASH & LHASH
1. Tại Client, gõ: `LHASH test.txt` -> Hiển thị SHA-256 của `test.txt` local.
2. Thực hiện `STOR test.txt`.
3. Gõ: `HASH test.txt` -> Server trả về SHA-256 của `test.txt` trên Server.
4. So sánh 2 chuỗi hash -> Khớp nhau tuyệt đối 100%.

### 2. Test Congestion Control & Go-Back-N với Packet Loss
1. Sửa `SIMULATE_PACKET_LOSS = true` trong `RdtPacket.h`.
2. Gửi file lớn qua `STOR` hoặc `RETR`.
3. Quan sát console thu được log timeout, co hẹp `cwnd`, gửi lại từ `base` nhưng file chuyển giao vẫn hoàn toàn chính xác.
