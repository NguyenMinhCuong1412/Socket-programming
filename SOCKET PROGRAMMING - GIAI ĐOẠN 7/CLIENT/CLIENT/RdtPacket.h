// ======================================================================
// RdtPacket.h — ĐỊNH NGHĨA GÓI TIN RDT (RELIABLE DATA TRANSFER) - CLIENT
//    Khai báo cấu trúc gói tin RdtPacket và các hằng số cấu hình cho
//    giao thức truyền tin cậy tự xây dựng trên nền UDP, sử dụng cơ chế
//    Go-Back-N với Sliding Window, AIMD (Additive Increase/Multiplicative
//    Decrease) để điều chỉnh kích thước cửa sổ, và Internet Checksum
//    (1's complement) để kiểm tra toàn vẹn dữ liệu.
//    File này giống nhau ở cả Client và Server.
// ======================================================================
#pragma once
#include "lib.h"



// === Cờ (flags) xác định loại gói tin RDT ===
// Mỗi gói tin chỉ mang đúng 1 trong 3 cờ sau (không kết hợp)
constexpr uint8_t FLAG_DATA = 0x01;  // Gói tin chứa dữ liệu (DATA)
constexpr uint8_t FLAG_ACK = 0x02;   // Gói tin xác nhận (ACK) — dùng ACK tích lũy (cumulative ACK)
constexpr uint8_t FLAG_FIN = 0x04;   // Gói tin báo kết thúc phiên truyền (FIN)

// === Các hằng số cấu hình giao thức RDT ===
constexpr int RDT_TIMEOUT_MS = 500;   // Thời gian chờ tối đa trước khi coi là timeout (ms) — kích hoạt retransmit
constexpr int RDT_POLL_MS = 50;       // Thời gian poll socket khi chờ ACK (ms) — SO_RCVTIMEO cho vòng lặp gửi
constexpr int RDT_MAX_RETRIES = 20;   // Số lần retry tối đa (Go-Back-N) trước khi báo lỗi truyền
constexpr int RDT_MAX_PAYLOAD = 1024; // Kích thước payload tối đa mỗi gói DATA (byte)
constexpr int RDT_HEADER_SIZE = 9;    // Kích thước header RDT cố định = 9 byte:
                                       //   4 byte seqNum + 1 byte flags + 2 byte checksum + 2 byte payloadLength

// === Tham số Sliding Window (Go-Back-N + AIMD) ===
constexpr int RDT_INITIAL_WINDOW = 4;  // Kích thước cửa sổ ban đầu khi bắt đầu truyền
constexpr int RDT_MIN_WINDOW = 1;      // Kích thước cửa sổ tối thiểu (sau khi giảm do timeout — Multiplicative Decrease)
constexpr int RDT_MAX_WINDOW = 32;     // Kích thước cửa sổ tối đa (giới hạn trên của Additive Increase)

// === Cấu hình giả lập mất gói tin (Packet Loss Simulation) ===
// Dùng để kiểm thử cơ chế RDT — bật SIMULATE_PACKET_LOSS = true để mô phỏng mất gói ngẫu nhiên
constexpr bool SIMULATE_PACKET_LOSS = false;  // false = tắt giả lập, true = bật giả lập mất gói
constexpr int  LOSS_PERCENT         = 10;     // Xác suất mất gói (%) — mỗi lần gửi/nhận có 10% cơ hội bị "drop"


// === Cấu trúc gói tin RDT ===
// Layout trên đường truyền (9 byte header + payload):
//   [0..3]  seqNum        (4 byte, network byte order — Big-Endian)
//   [4]     flags         (1 byte — FLAG_DATA/FLAG_ACK/FLAG_FIN)
//   [5..6]  checksum      (2 byte, network byte order — Internet Checksum 1's complement)
//   [7..8]  payloadLength (2 byte, network byte order)
//   [9..]   payload       (0..RDT_MAX_PAYLOAD byte dữ liệu)
struct RdtPacket {
    uint32_t seqNum;         // Sequence number — số thứ tự gói tin (đánh số từ 0)
    uint8_t  flags;          // Cờ loại gói tin: FLAG_DATA, FLAG_ACK hoặc FLAG_FIN
    uint16_t checksum;       // Internet Checksum (1's complement) của toàn bộ gói (header + payload)
    uint16_t payloadLength;  // Độ dài phần payload (byte), không tính header
    vector<char> payload;    // Dữ liệu payload thực tế (chỉ có ý nghĩa với gói DATA)
};

// Tính Internet Checksum (1's complement) cho một khối dữ liệu thô
uint16_t computeChecksum(const char*, int);

// Kiểm tra checksum: tính lại checksum trên toàn bộ dữ liệu (bao gồm cả trường checksum)
// Nếu kết quả = 0 → dữ liệu toàn vẹn, ngược lại → bị hỏng
bool verifyChecksum(const char*, int);

// Đóng gói (serialize) RdtPacket thành mảng byte để gửi qua UDP
// Chuyển các trường số sang network byte order (Big-Endian) bằng htonl/htons
vector<char> serializePacket(const RdtPacket&);

// Giải gói (deserialize) mảng byte nhận được từ UDP thành RdtPacket
// Kiểm tra tính hợp lệ: kích thước, flags, payload length, checksum
bool deserializePacket(const char*, int, RdtPacket&);

// Quyết định có giả lập mất gói hay không (dựa vào SIMULATE_PACKET_LOSS và LOSS_PERCENT)
// Dùng bộ sinh số ngẫu nhiên thread_local để an toàn đa luồng
bool shouldSimulateLoss();
