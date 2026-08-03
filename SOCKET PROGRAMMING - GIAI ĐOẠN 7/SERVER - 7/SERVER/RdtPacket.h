#pragma once
#include "lib.h"

//RdtPacket.h — Reliable Data Transfer Packet
//Go-Back-N (Sliding Window) + Congestion Control (AIMD) trên nền UDP


//Flags (bit-field trong 1 byte)
constexpr uint8_t FLAG_DATA = 0x01; //Gói mang dữ liệu
constexpr uint8_t FLAG_ACK  = 0x02; //Gói xác nhận (seqNum = cumulative ACK: seq lớn nhất đã nhận liên tục)
constexpr uint8_t FLAG_FIN  = 0x04; //Gói báo kết thúc truyền (seqNum = tổng số gói DATA đã gửi)

//Hằng số cấu hình RDT
constexpr int RDT_TIMEOUT_MS   = 500;  //Timeout thật (đo bằng đồng hồ) chờ ACK cho gói cũ nhất chưa được ACK
constexpr int RDT_POLL_MS      = 50;   //Chu kỳ poll recvfrom() - đủ ngắn để vừa gửi thêm gói trong cửa sổ, vừa kiểm tra timeout kịp thời
constexpr int RDT_MAX_RETRIES  = 20;   //Số vòng Go-Back-N tối đa trước khi báo lỗi (mỗi vòng gửi lại cả cửa sổ)
constexpr int RDT_MAX_PAYLOAD  = 1024; //Kích thước payload tối đa (byte) = CHUNK_SIZE
constexpr int RDT_HEADER_SIZE  = 9;    //seqNum(4) + flags(1) + checksum(2) + payloadLength(2) = 9 bytes

//Hằng số Sliding Window / Congestion Control (AIMD: Additive Increase, Multiplicative Decrease)
constexpr int RDT_INITIAL_WINDOW = 4;  //Số gói tối đa được phép "bay" (đã gửi, chưa ACK) cùng lúc lúc khởi đầu
constexpr int RDT_MIN_WINDOW     = 1;  //Cửa sổ tối thiểu (rớt về tương đương Stop-and-Wait khi mạng quá tệ)
constexpr int RDT_MAX_WINDOW     = 32; //Cửa sổ tối đa (chặn trên để không làm ngập mạng/receiver)

//Packet Loss Simulation
constexpr bool SIMULATE_PACKET_LOSS = false; //true = bật giả lập mất gói để test
constexpr int  LOSS_PERCENT         = 10;    //Tỉ lệ mất gói (%) khi bật simulation


// Cấu trúc RdtPacket — chỉ dùng trong bộ nhớ, KHÔNG gửi trực tiếp
struct RdtPacket {
    uint32_t seqNum;           //DATA: số thứ tự tăng dần 0,1,2,...  |  ACK: cumulative ACK (seq lớn nhất đã nhận liên tục)  |  FIN: tổng số gói DATA đã gửi
    uint8_t  flags;            //Tổ hợp FLAG_DATA / FLAG_ACK / FLAG_FIN
    uint16_t checksum;         //Internet checksum (1's complement)
    uint16_t payloadLength;    //Số byte payload thực tế (0 nếu ACK/FIN thuần)
    vector<char> payload; //Dữ liệu thực
};


//Hàm tính checksum kiểu Internet (1's complement sum)
//Input: mảng byte đã serialize (với field checksum = 0)
//Output: giá trị checksum 16-bit
uint16_t computeChecksum(const char* data, int length);


//Kiểm tra checksum: tính lại trên toàn bộ packet (bao gồm field checksum) → kết quả phải = 0 nếu packet không bị lỗi
bool verifyChecksum(const char* data, int length);

//Serialize: RdtPacket → mảng byte (ghi từng field theo thứ tự)
//Thứ tự: seqNum(4B) | flags(1B) | checksum(2B) | payloadLength(2B) | payload
//Checksum được tính SAU khi ghi tất cả field khác (checksum=0 lúc tính)
vector<char> serializePacket(const RdtPacket& pkt);

//Deserialize: mảng byte → RdtPacket
//Kiểm tra:
//   - Đủ dữ liệu cho header (9 byte tối thiểu)
//   - payloadLength khớp với dữ liệu thực nhận
//   - Checksum hợp lệ
//Return: true nếu thành công, false nếu packet lỗi
bool deserializePacket(const char* data, int length, RdtPacket& outPkt);

//Packet loss simulation: trả về true nếu nên "bỏ" gói này
//Chỉ hoạt động khi SIMULATE_PACKET_LOSS = true
bool shouldSimulateLoss();
