# Walkthrough — Giai đoạn 6: Reliable Data Transfer

## Tổng quan thay đổi

Biến UDP thô thành **UDP Reliable** bằng **Stop-and-Wait ARQ**, hoàn toàn trong tầng DataChannel. Không sửa CommandHandler, ControlChannel, Session, hay bất kỳ file nào khác.

---

## File đã tạo mới

### 1. RdtPacket.h + RdtPacket.cpp (cả SERVER lẫn CLIENT — giống hệt nhau)

**Chứa gì:**
- **Struct `RdtPacket`**: seqNum (4B) + flags (1B) + checksum (2B) + payloadLength (2B) + payload — tổng header 9 byte
- **Flags**: `FLAG_DATA=0x01`, `FLAG_ACK=0x02`, `FLAG_FIN=0x04`
- **Hằng số cấu hình**: `RDT_TIMEOUT_MS=500`, `RDT_MAX_RETRIES=20`, `RDT_MAX_PAYLOAD=1024`
- **`computeChecksum()`**: Internet checksum — cộng cặp byte, fold carry, đảo bit
- **`verifyChecksum()`**: Tính lại trên toàn bộ packet → phải = 0 nếu không lỗi
- **`serializePacket()`**: Ghi từng field theo thứ tự vào mảng byte (network byte order), tính checksum sau cùng
- **`deserializePacket()`**: Đọc từng field, kiểm tra header đủ, payloadLength khớp, checksum hợp lệ
- **`shouldSimulateLoss()`**: Random drop packet theo tỉ lệ `LOSS_PERCENT` (mặc định tắt)

---

## File đã sửa

### 2. DataChannel.h (cả SERVER lẫn CLIENT — giống hệt nhau)

| Thay đổi | Chi tiết |
|----------|----------|
| Thêm `#include "RdtPacket.h"` | Để dùng struct RdtPacket và các hàm serialize/checksum |
| Thêm `rdtSend()` private | Gửi buffer qua Stop-and-Wait: chia chunk → DATA+ACK → FIN |
| Thêm `rdtReceive()` private | Nhận buffer qua Stop-and-Wait: DATA → ACK → đến FIN → trả buffer |
| **Interface public giữ nguyên 100%** | `start()`, `sendFile()`, `receiveFile()`, `sendProbe()`, `sendFileAfterHandshake()`, `stop()` |

### 3. DataChannel.cpp (cả SERVER lẫn CLIENT — giống hệt nhau)

| Method | Trước (GĐ5) | Sau (GĐ6) |
|--------|-------------|------------|
| `sendFile()` | `sendto()` từng chunk + gói 0-byte EOF | Đọc toàn bộ file → `rdtSend()` (tự chia chunk, gửi FIN) |
| `receiveFile()` | Vòng lặp `recvfrom()` + gói 0-byte = EOF | `rdtReceive()` → nhận toàn bộ data → ghi file |
| `sendProbe()` | `sendto()` 1 byte "R" raw | `rdtSend()` 1 byte "R" qua RDT |
| `sendFileAfterHandshake()` | `recvfrom()` probe raw → `sendFile()` | `rdtReceive()` probe → học địa chỉ → `sendFile()` qua RDT |
| `start()` | Giữ nguyên | Giữ nguyên |
| `stop()` | Giữ nguyên | Giữ nguyên |

### 4. SERVER.vcxproj + CLIENT.vcxproj

Thêm `RdtPacket.cpp` vào `<ClCompile>` và `RdtPacket.h` vào `<ClInclude>`.

---

## File KHÔNG SỬA (xác nhận giữ nguyên)

| File | Lý do |
|------|-------|
| lib.h (cả 2) | Không cần thay đổi hằng số |
| Server.cpp / Client.cpp | main() không liên quan |
| ControlChannel.h/.cpp (cả 2) | Kênh TCP, không đi qua UDP |
| Session.h/.cpp | Trạng thái phiên, gọi DataChannel qua interface public cũ |
| CmdHandler.h/.cpp | Gọi DataChannel qua interface public cũ — KHÔNG cần sửa |

---

## Luồng RDT mới (Stop-and-Wait ARQ)

```
SENDER (rdtSend)                          RECEIVER (rdtReceive)
──────────────                            ────────────────────
seq=0                                     expectedSeq=0

┌─────────────────────┐
│ Tạo DATA(seq=0)     │  ──sendto──►      recvfrom → deserialize
│ serialize + checksum │                   checksum OK?
│                     │                      ├─ NO  → DROP (no ACK)
│                     │  ◄──recvfrom──      ├─ YES → seq==expected?
│ Nhận ACK(seq=0)?   │                      │    ├─ YES → deliver + ACK(0) + expectedSeq=1
│   ├─ YES → seq=1   │                      │    └─ NO  → ACK(seq nhận) nhưng không deliver
│   ├─ TIMEOUT → retry│
│   └─ BAD → ignore  │
└─────────────────────┘

... lặp lại cho từng chunk ...

┌─────────────────────┐
│ Gửi FIN(seq=N)      │  ──sendto──►      Nhận FIN → ACK(N) → break
│ Chờ ACK cho FIN    │  ◄──recvfrom──
└─────────────────────┘
```

---

## Cách test

### Functional Test
1. Mở Visual Studio → Build cả SERVER và CLIENT → phải **0 errors, 0 new warnings**
2. Chạy SERVER → chạy CLIENT → login → thử STOR, RETR, STOU, APPE, LIST, NLST
3. So sánh file gửi/nhận bằng `fc /b` → phải giống nhau 100%

### Packet Loss Test
1. Trong `RdtPacket.h`, sửa `SIMULATE_PACKET_LOSS = true`
2. Build lại cả 2
3. Chạy STOR/RETR → phải thấy log `[RDT] Timeout...`, `[RDT-SIM] Dropped...` nhưng transfer vẫn thành công
4. Sau khi test xong, sửa lại `SIMULATE_PACKET_LOSS = false`
