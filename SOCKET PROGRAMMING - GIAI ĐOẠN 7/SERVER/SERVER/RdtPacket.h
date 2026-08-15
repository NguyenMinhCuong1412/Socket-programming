// ======================================================================
// RdtPacket.h — ĐỊNH NGHĨA GÓI TIN RDT (RELIABLE DATA TRANSFER) - SERVER
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
constexpr uint8_t FLAG_ACK  = 0x02;  // Gói tin xác nhận (ACK) — dùng ACK tích lũy (cumulative ACK)
constexpr uint8_t FLAG_FIN  = 0x04;  // Gói tin báo kết thúc phiên truyền (FIN)

// === Các hằng số cấu hình giao thức RDT ===
constexpr int RDT_TIMEOUT_MS   = 500;   // Thời gian chờ tối đa trước khi timeout (ms)
constexpr int RDT_POLL_MS      = 50;    // Thời gian poll socket khi chờ ACK (ms)
constexpr int RDT_MAX_RETRIES  = 20;    // Số lần retry tối đa (Go-Back-N)
constexpr int RDT_MAX_PAYLOAD  = 1024;  // Kích thước payload tối đa mỗi gói DATA (byte)
constexpr int RDT_HEADER_SIZE  = 9;     // Kích thước header RDT = 9 byte:
                                         //   4 byte seqNum + 1 byte flags + 2 byte checksum + 2 byte payloadLength

// === Tham số Sliding Window (Go-Back-N + AIMD) ===
constexpr int RDT_INITIAL_WINDOW = 4;  // Kích thước cửa sổ ban đầu
constexpr int RDT_MIN_WINDOW     = 1;  // Cửa sổ tối thiểu (Multiplicative Decrease)
constexpr int RDT_MAX_WINDOW     = 32; // Cửa sổ tối đa (Additive Increase)

// === Giả lập mất gói tin (Packet Loss Simulation) ===
constexpr bool SIMULATE_PACKET_LOSS = false;  // false = tắt, true = bật giả lập mất gói
constexpr int  LOSS_PERCENT         = 10;     // Xác suất mất gói (%)


// === Cấu trúc gói tin RDT ===
// Layout header 9 byte: [seqNum:4B][flags:1B][checksum:2B][payloadLength:2B] + payload
struct RdtPacket {
    uint32_t seqNum;         // Sequence number — số thứ tự gói tin
    uint8_t  flags;          // Cờ loại gói tin: FLAG_DATA, FLAG_ACK hoặc FLAG_FIN
    uint16_t checksum;       // Internet Checksum (1's complement) của toàn bộ gói
    uint16_t payloadLength;  // Độ dài payload (byte)
    vector<char> payload;    // Dữ liệu payload thực tế
};

// Tính Internet Checksum cho khối dữ liệu thô
uint16_t computeChecksum(const char*, int);

// Kiểm tra checksum toàn bộ gói (bao gồm trường checksum) — trả về true nếu toàn vẹn
bool verifyChecksum(const char*, int);

// Serialize RdtPacket thành mảng byte (network byte order) để gửi qua UDP
vector<char> serializePacket(const RdtPacket&);

// Deserialize mảng byte nhận từ UDP thành RdtPacket — kiểm tra kích thước, flags, checksum
bool deserializePacket(const char*, int, RdtPacket&);

// Quyết định có giả lập mất gói hay không (thread-safe với thread_local RNG)
bool shouldSimulateLoss();
