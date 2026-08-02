#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <iostream>
#include <random>
#include <winsock2.h>

// ============================================================
// RdtPacket.h — Reliable Data Transfer Packet
// Giai đoạn 6: Stop-and-Wait ARQ trên nền UDP
// ============================================================

// ----- Flags (bit-field trong 1 byte) -----
constexpr uint8_t FLAG_DATA = 0x01; // Gói mang dữ liệu
constexpr uint8_t FLAG_ACK  = 0x02; // Gói xác nhận
constexpr uint8_t FLAG_FIN  = 0x04; // Gói báo kết thúc truyền

// ----- Hằng số cấu hình RDT -----
constexpr int RDT_TIMEOUT_MS   = 500;  // Timeout chờ ACK (mili-giây)
constexpr int RDT_MAX_RETRIES  = 20;   // Số lần gửi lại tối đa trước khi báo lỗi
constexpr int RDT_MAX_PAYLOAD  = 1024; // Kích thước payload tối đa (byte) = CHUNK_SIZE
constexpr int RDT_HEADER_SIZE  = 9;    // seqNum(4) + flags(1) + checksum(2) + payloadLength(2)

// ----- Packet Loss Simulation -----
constexpr bool SIMULATE_PACKET_LOSS = false; // true = bật giả lập mất gói để test
constexpr int  LOSS_PERCENT         = 10;    // Tỉ lệ mất gói (%) khi bật simulation

// ============================================================
// Cấu trúc RdtPacket — chỉ dùng trong bộ nhớ, KHÔNG gửi trực tiếp
// Phải serialize thành mảng byte trước khi sendto()
// ============================================================
struct RdtPacket {
    uint32_t seqNum;        // Sequence number (0 hoặc 1 cho Stop-and-Wait)
    uint8_t  flags;         // Tổ hợp FLAG_DATA / FLAG_ACK / FLAG_FIN
    uint16_t checksum;      // Internet checksum (1's complement)
    uint16_t payloadLength; // Số byte payload thực tế (0 nếu ACK/FIN thuần)
    std::vector<char> payload; // Dữ liệu thực
};

// ============================================================
// Hàm tính checksum kiểu Internet (1's complement sum)
// Input: mảng byte đã serialize (với field checksum = 0)
// Output: giá trị checksum 16-bit
// ============================================================
uint16_t computeChecksum(const char* data, int length);

// ============================================================
// Kiểm tra checksum: tính lại trên toàn bộ packet (bao gồm
// field checksum) → kết quả phải = 0 nếu packet không bị lỗi
// ============================================================
bool verifyChecksum(const char* data, int length);

// ============================================================
// Serialize: RdtPacket → mảng byte (ghi từng field theo thứ tự)
// Thứ tự: seqNum(4B) | flags(1B) | checksum(2B) | payloadLength(2B) | payload
// Checksum được tính SAU khi ghi tất cả field khác (checksum=0 lúc tính)
// ============================================================
std::vector<char> serializePacket(const RdtPacket& pkt);

// ============================================================
// Deserialize: mảng byte → RdtPacket
// Kiểm tra:
//   - Đủ dữ liệu cho header (9 byte tối thiểu)
//   - payloadLength khớp với dữ liệu thực nhận
//   - Checksum hợp lệ
// Return: true nếu thành công, false nếu packet lỗi
// ============================================================
bool deserializePacket(const char* data, int length, RdtPacket& outPkt);

// ============================================================
// Packet loss simulation: trả về true nếu nên "bỏ" gói này
// Chỉ hoạt động khi SIMULATE_PACKET_LOSS = true
// ============================================================
bool shouldSimulateLoss();
