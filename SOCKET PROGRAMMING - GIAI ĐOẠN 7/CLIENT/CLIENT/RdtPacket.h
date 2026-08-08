#pragma once
#include "lib.h"

//RdtPacket — Reliable Data Transfer Packet
//Go-Back-N (Sliding Window) + Congestion Control (AIMD) trên nền UDP

//uint[N]_t: Unsigned (u) + Interger (int) + [N] (số bit) + Type (_t: kí hiệu tiêu chuẩn đánh dấu một kiểu dữ liệu)
//0x[...]: ký hiệu đánh dấu hệ 16 - 1 ký tự 4 bit; [...] - số lượng ký tự hệ 16 tùy vào số bit [N] = N/4 

//Flags (bit-field trong 1 byte)
constexpr uint8_t FLAG_DATA = 0x01; //DATA - Gói dữ liệu
constexpr uint8_t FLAG_ACK = 0x02; //ACKnowledgment - Gói xác nhận (seqNum = cumulative ACK: seq lớn nhất đã nhận liên tục) 
constexpr uint8_t FLAG_FIN = 0x04; //FINish - Gói kết thúc truyền (seqNum = tổng số gói DATA đã gửi)

//Hằng số cấu hình RDT
constexpr int RDT_TIMEOUT_MS = 500;  //Thời gian chờ phản hồi ACK cho gói cũ nhất chưa được ACK (500 millisecond = 0.5 second)
constexpr int RDT_POLL_MS = 50;   //Chu kỳ poll recvfrom() - Chu kỳ kiểm tra socket (50 millisecond) đủ ngắn để vừa gửi thêm gói trong cửa sổ, vừa kiểm tra timeout kịp thời
constexpr int RDT_MAX_RETRIES = 20;   //Số vòng Go-Back-N tối đa trước khi báo lỗi (mỗi vòng gửi lại cả cửa sổ)
constexpr int RDT_MAX_PAYLOAD = 1024; //Dung lượng dữ liệu thực sự (Payload) tối đa (byte) = CHUNK_SIZE
constexpr int RDT_HEADER_SIZE = 9;    //seqNum(4) + flags(1) + checksum(2) + payloadLength(2) = 9 bytes

//Hằng số Sliding Window / Congestion Control (AIMD: Additive Increase, Multiplicative Decrease)
constexpr int RDT_INITIAL_WINDOW = 4;  //Số gói tối đa được phép gửi (đã gửi, chưa ACK) cùng lúc lúc khởi đầu
constexpr int RDT_MIN_WINDOW = 1;  //Cửa sổ tối thiểu (rớt về tương đương Stop-and-Wait khi mạng quá tệ)
constexpr int RDT_MAX_WINDOW = 32; //Cửa sổ tối đa (chặn trên để không làm ngập mạng/receiver)

//Packet Loss Simulation
constexpr bool SIMULATE_PACKET_LOSS = false; //true = bật giả lập mất gói để test
constexpr int  LOSS_PERCENT         = 10;    //Tỉ lệ mất gói (%) khi bật simulation


//Cấu trúc RdtPacket — chỉ dùng trong bộ nhớ, KHÔNG gửi trực tiếp
struct RdtPacket {
    uint32_t seqNum;        //DATA: số thứ tự tăng dần 0,1,2,... / ACK: cumulative ACK (seq lớn nhất đã nhận liên tục) / FIN: tổng số gói DATA đã gửi
    uint8_t  flags;         //Tổ hợp FLAG_DATA / FLAG_ACK / FLAG_FIN
    uint16_t checksum;      //Internet checksum (1's complement)
    uint16_t payloadLength; //Số byte payload thực tế (0 nếu ACK/FIN thuần)
    vector<char> payload;   //Dữ liệu thực
};

//computeChecksum — Tính Internet checksum (1's complement sum)
//Input: mảng byte đã serialize (với field checksum = 0)
//Output: giá trị checksum 16-bit
//Thuật toán:
//   1. Cộng từng cặp 2 byte (big-endian) vào biến sum 32-bit
//   2. Nếu số byte lẻ -> byte cuối được pad thêm 0x00
//   3. Fold carry: cộng phần tràn 16 bit cao vào 16 bit thấp
//   4. Đảo bit (~) kết quả -> trả về
//LƯU Ý: Field checksum trong packet phải = 0 trước khi gọi hàm này
uint16_t computeChecksum(const char*, int);

//verifyChecksum — Kiểm tra checksum của packet đã serialize
//Cách hoạt động: tính checksum trên TOÀN BỘ packet (bao gồm cả field checksum đã có giá trị) → nếu packet không bị lỗi thì kết quả phải = 0
bool verifyChecksum(const char*, int);

//serializePacket — Chuyển RdtPacket thành mảng byte
//Thứ tự ghi:
//   [0..3]  seqNum        (4 byte, network byte order)
//   [4]     flags         (1 byte)
//   [5..6]  checksum      (2 byte, network byte order) — ghi 0 trước, tính sau
//   [7..8]  payloadLength (2 byte, network byte order)
//   [9..]   payload       (payloadLength byte)
//Sau khi ghi xong tất cả field (checksum=0), tính checksum trên toàn bộ buffer, rồi ghi ngược lại vào vị trí [5..6]
vector<char> serializePacket(const RdtPacket&);

//deserializePacket — Chuyển mảng byte thành RdtPacket
//Kiểm tra:
//   1. Đủ dữ liệu cho header (tối thiểu 9 byte)
//   2. payloadLength + header = tổng length nhận được
//   3. Checksum hợp lệ (verifyChecksum)
//Return false nếu bất kỳ kiểm tra nào thất bại
bool deserializePacket(const char*, int, RdtPacket&);

//shouldSimulateLoss — Quyết định có "bỏ" gói này không
//Dùng random engine thread-local để thread-safe
//Chỉ trả về true khi SIMULATE_PACKET_LOSS = true
bool shouldSimulateLoss();
