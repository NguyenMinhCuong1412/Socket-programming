// ======================================================================
// RdtPacket.cpp — CÀI ĐẶT ĐÓNG GÓI / GIẢI GÓI / CHECKSUM CHO GÓI TIN RDT - CLIENT
//    Triển khai các hàm xử lý gói tin RDT: tính/kiểm tra Internet Checksum
//    (1's complement), serialize/deserialize gói tin với network byte order,
//    và hàm giả lập mất gói tin để kiểm thử cơ chế Go-Back-N.
//    File này giống nhau ở cả Client và Server.
// ======================================================================
#include "RdtPacket.h"

// Tính Internet Checksum (RFC 1071) theo phương pháp 1's complement:
// 1. Chia dữ liệu thành các word 16-bit, cộng dồn vào biến sum 32-bit
// 2. Nếu còn dư 1 byte lẻ → padding thêm 0x00 phía sau rồi cộng
// 3. Fold carry: cộng 16-bit cao vào 16-bit thấp cho đến khi hết carry
// 4. Lấy bù 1 (~) kết quả → trả về checksum 16-bit
// Khi tính checksum cho gói gửi đi: trường checksum trong header = 0 trước khi tính
uint16_t computeChecksum(const char* data, int length) {
    uint32_t sum = 0;  // Dùng 32-bit để chứa carry khi cộng

    int i = 0;
    // Cộng dồn từng cặp 2 byte (1 word 16-bit) — byte cao << 8 | byte thấp
    while (i + 1 < length) {
        uint16_t word = ((uint8_t)data[i] << 8) | (uint8_t)data[i + 1];
        sum += word;
        i += 2;
    }

    // Nếu dữ liệu có số byte lẻ → byte cuối cùng được đặt ở byte cao, byte thấp = 0
    if (i < length) {
        uint16_t word = (uint8_t)data[i] << 8;
        sum += word;
    }

    // Fold carry: cộng phần carry (16-bit cao) vào phần kết quả (16-bit thấp)
    // Lặp cho đến khi không còn carry nào (sum nằm gọn trong 16-bit)
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    // Lấy bù 1 (bitwise NOT) → kết quả checksum
    return ~(uint16_t)sum;
}

// Kiểm tra tính toàn vẹn dữ liệu bằng Internet Checksum:
// Tính lại checksum trên TOÀN BỘ gói (bao gồm cả trường checksum đã có sẵn).
// Nếu dữ liệu không bị hỏng, tổng 1's complement = 0xFFFF → ~sum = 0
bool verifyChecksum(const char* data, int length) {
    uint32_t sum = 0;
    int i = 0;
    while (i + 1 < length) {
        uint16_t word = ((uint8_t)data[i] << 8) | (uint8_t)data[i + 1];
        sum += word;
        i += 2;
    }
    if (i < length) {
        uint16_t word = (uint8_t)data[i] << 8;
        sum += word;
    }
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    // Nếu ~sum == 0 → tổng 1's complement đúng → dữ liệu toàn vẹn
    return ((uint16_t)~sum == 0);
}

// Đóng gói (serialize) RdtPacket thành mảng byte để gửi qua mạng.
// Layout header 9 byte: [seqNum: 4B][flags: 1B][checksum: 2B][payloadLength: 2B]
// Tất cả trường số đều được chuyển sang network byte order (Big-Endian) bằng htonl/htons
vector<char> serializePacket(const RdtPacket& pkt) {
    int totalSize = RDT_HEADER_SIZE + (int)pkt.payload.size();
    vector<char> buf(totalSize, 0);  // Khởi tạo toàn bộ = 0 (quan trọng cho checksum)

    // --- Ghi seqNum (4 byte, Big-Endian) ---
    // htonl: chuyển host byte order → network byte order (Big-Endian) cho số 32-bit
    uint32_t netSeq = htonl(pkt.seqNum);
    // Tách 4 byte từ giá trị 32-bit đã chuyển sang Big-Endian, ghi lần lượt vào buf[0..3]
    buf[0] = (char)((netSeq >> 24) & 0xFF);  // Byte cao nhất (MSB)
    buf[1] = (char)((netSeq >> 16) & 0xFF);
    buf[2] = (char)((netSeq >> 8) & 0xFF);
    buf[3] = (char)(netSeq & 0xFF);           // Byte thấp nhất (LSB)

    // --- Ghi flags (1 byte) ---
    buf[4] = (char)pkt.flags;
    // --- Trường checksum tạm đặt = 0 (sẽ tính sau khi có đủ dữ liệu) ---
    buf[5] = 0; buf[6] = 0;

    // --- Ghi payloadLength (2 byte, Big-Endian) ---
    // htons: chuyển host → network byte order cho số 16-bit
    uint16_t netLen = htons((uint16_t)pkt.payload.size());
    buf[7] = (char)((netLen >> 8) & 0xFF);  // Byte cao
    buf[8] = (char)(netLen & 0xFF);          // Byte thấp

    // --- Sao chép payload vào sau header ---
    for (int i = 0; i < (int)pkt.payload.size(); i++) buf[RDT_HEADER_SIZE + i] = pkt.payload[i];
    // --- Tính checksum trên toàn bộ gói (header có checksum=0 + payload) ---
    uint16_t cksum = computeChecksum(buf.data(), totalSize);

    // --- Ghi checksum vào vị trí đã dành sẵn (buf[5..6]), Big-Endian ---
    uint16_t netCksum = htons(cksum);
    buf[5] = (char)((netCksum >> 8) & 0xFF);
    buf[6] = (char)(netCksum & 0xFF);

    return buf;
}

// Giải gói (deserialize) mảng byte nhận từ mạng thành RdtPacket.
// Kiểm tra nhiều điều kiện hợp lệ: kích thước tối thiểu, flags hợp lệ,
// payload length khớp, và checksum đúng. Trả về false nếu gói bị lỗi.
bool deserializePacket(const char* data, int length, RdtPacket& outPkt) {
    // Kiểm tra gói có đủ header 9 byte không
    if (length < RDT_HEADER_SIZE) {
        cerr << format("[RDT] Packet too short: {} bytes (need {})", length, RDT_HEADER_SIZE) << endl;
        return false;
    }

    // --- Đọc seqNum (4 byte) → ghép thành uint32_t Big-Endian → ntohl chuyển về host order ---
    uint32_t netSeq = 0;
    netSeq |= ((uint32_t)(uint8_t)data[0] << 24);  // Byte 0 = MSB, dịch trái 24 bit
    netSeq |= ((uint32_t)(uint8_t)data[1] << 16);  // Byte 1, dịch trái 16 bit
    netSeq |= ((uint32_t)(uint8_t)data[2] << 8);   // Byte 2, dịch trái 8 bit
    netSeq |= ((uint32_t)(uint8_t)data[3]);         // Byte 3 = LSB
    // ntohl: chuyển network byte order (Big-Endian) → host byte order
    outPkt.seqNum = ntohl(netSeq);

    // --- Đọc flags (1 byte) và kiểm tra hợp lệ ---
    outPkt.flags = (uint8_t)data[4];
    if (outPkt.flags != FLAG_DATA && outPkt.flags != FLAG_ACK && outPkt.flags != FLAG_FIN) {
        cerr << format("[RDT] Invalid packet flags: {:#x}", outPkt.flags) << endl;
        return false;
    }

    // --- Đọc checksum (2 byte, Big-Endian → host order) ---
    uint16_t netCksum = 0;
    netCksum |= ((uint16_t)(uint8_t)data[5] << 8);  // Byte cao
    netCksum |= ((uint16_t)(uint8_t)data[6]);        // Byte thấp
    outPkt.checksum = ntohs(netCksum);  // ntohs: network → host cho 16-bit

    // --- Đọc payloadLength (2 byte, Big-Endian → host order) ---
    uint16_t netLen = 0;
    netLen |= ((uint16_t)(uint8_t)data[7] << 8);
    netLen |= ((uint16_t)(uint8_t)data[8]);
    outPkt.payloadLength = ntohs(netLen);

    // Kiểm tra payloadLength khai báo trong header có khớp với kích thước thực nhận không
    if (outPkt.payloadLength != length - RDT_HEADER_SIZE) {
        cerr << format("[RDT] Payload length mismatch: header says {} but received {} bytes",
            outPkt.payloadLength, (length - RDT_HEADER_SIZE)) << endl;
        return false;
    }

    // Kiểm tra toàn vẹn dữ liệu bằng Internet Checksum
    if (!verifyChecksum(data, length)) {
        cerr << "[RDT] Checksum verification failed" << endl;
        return false;
    }

    // Trích xuất payload từ vị trí sau header
    outPkt.payload.assign(data + RDT_HEADER_SIZE, data + length);
    return true;
}

// Quyết định có giả lập mất gói tin hay không.
// Nếu SIMULATE_PACKET_LOSS = false → luôn trả về false (không mất gói).
// Nếu bật → dùng bộ sinh số ngẫu nhiên Mersenne Twister (mt19937) với phân phối đều [0, 99]
// để quyết định: nếu số ngẫu nhiên < LOSS_PERCENT → coi như gói bị mất (drop).
// Sử dụng thread_local để mỗi thread có bộ sinh riêng — tránh race condition khi đa luồng.
bool shouldSimulateLoss() {
    if (!SIMULATE_PACKET_LOSS) return false;

    thread_local mt19937 rng(random_device{}());            // Bộ sinh số ngẫu nhiên riêng cho mỗi thread
    thread_local uniform_int_distribution<int> dist(0, 99); // Phân phối đều từ 0 đến 99

    return dist(rng) < LOSS_PERCENT;  // true nếu số ngẫu nhiên < ngưỡng → "mất gói"
}
