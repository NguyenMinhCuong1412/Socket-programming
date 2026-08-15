// ======================================================================
// RdtPacket.cpp — CÀI ĐẶT ĐÓNG GÓI / GIẢI GÓI / CHECKSUM CHO GÓI TIN RDT - SERVER
//    Triển khai các hàm xử lý gói tin RDT: tính/kiểm tra Internet Checksum,
//    serialize/deserialize gói tin, và giả lập mất gói tin.
//    File này giống nhau ở cả Client và Server.
// ======================================================================
#include "RdtPacket.h"

// Tính Internet Checksum (RFC 1071) theo phương pháp 1's complement:
// 1. Ghép từng cặp 2 byte thành word 16-bit (Big-Endian), cộng dồn vào sum 32-bit
// 2. Byte lẻ cuối cùng: padding 0x00 phía sau
// 3. Fold carry: cộng 16-bit cao vào 16-bit thấp cho đến khi hết carry
// 4. Lấy bù 1 (~) → trả về checksum 16-bit
uint16_t computeChecksum(const char* data, int length) {
    uint32_t sum = 0;

    int i = 0;
    // Cộng dồn từng word 16-bit
    while (i + 1 < length) {
        uint16_t word = ((uint8_t)data[i] << 8) | (uint8_t)data[i + 1];
        sum += word;
        i += 2;
    }

    // Xử lý byte lẻ cuối cùng
    if (i < length) {
        uint16_t word = (uint8_t)data[i] << 8;
        sum += word;
    }

    // Fold carry
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return ~(uint16_t)sum;
}

// Kiểm tra checksum: tính lại trên toàn bộ gói (bao gồm trường checksum)
// Nếu dữ liệu toàn vẹn → tổng = 0xFFFF → ~sum = 0
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
    return ((uint16_t)~sum == 0);
}

// Serialize RdtPacket thành mảng byte để gửi qua UDP
// Các trường số được chuyển sang network byte order (Big-Endian) bằng htonl/htons
vector<char> serializePacket(const RdtPacket& pkt) {
    int totalSize = RDT_HEADER_SIZE + (int)pkt.payload.size();
    vector<char> buf(totalSize, 0);

    // Ghi seqNum (4 byte, Big-Endian) — htonl chuyển host → network byte order
    uint32_t netSeq = htonl(pkt.seqNum);
    buf[0] = (char)((netSeq >> 24) & 0xFF);
    buf[1] = (char)((netSeq >> 16) & 0xFF);
    buf[2] = (char)((netSeq >> 8) & 0xFF);
    buf[3] = (char)(netSeq & 0xFF);

    // Ghi flags (1 byte)
    buf[4] = (char)pkt.flags;
    // Checksum tạm = 0 (sẽ tính sau)
    buf[5] = 0; buf[6] = 0;

    // Ghi payloadLength (2 byte, Big-Endian) — htons chuyển host → network
    uint16_t netLen = htons((uint16_t)pkt.payload.size());
    buf[7] = (char)((netLen >> 8) & 0xFF);
    buf[8] = (char)(netLen & 0xFF);

    // Sao chép payload vào sau header
    for (int i = 0; i < (int)pkt.payload.size(); i++) buf[RDT_HEADER_SIZE + i] = pkt.payload[i];
    // Tính checksum trên toàn bộ gói (header có checksum=0 + payload)
    uint16_t cksum = computeChecksum(buf.data(), totalSize);

    // Ghi checksum vào header (Big-Endian)
    uint16_t netCksum = htons(cksum);
    buf[5] = (char)((netCksum >> 8) & 0xFF);
    buf[6] = (char)(netCksum & 0xFF);

    return buf;
}

// Deserialize mảng byte nhận từ UDP thành RdtPacket
// Kiểm tra: kích thước header, flags hợp lệ, payload length, checksum
bool deserializePacket(const char* data, int length, RdtPacket& outPkt) {
    if (length < RDT_HEADER_SIZE) {
        cerr << format("[RDT] Packet too short: {} bytes (need {})", length, RDT_HEADER_SIZE) << endl;
        return false;
    }

    // Đọc seqNum (4 byte Big-Endian → ntohl chuyển về host byte order)
    uint32_t netSeq = 0;
    netSeq |= ((uint32_t)(uint8_t)data[0] << 24);
    netSeq |= ((uint32_t)(uint8_t)data[1] << 16);
    netSeq |= ((uint32_t)(uint8_t)data[2] << 8);
    netSeq |= ((uint32_t)(uint8_t)data[3]);
    outPkt.seqNum = ntohl(netSeq);

    // Đọc flags và kiểm tra hợp lệ
    outPkt.flags = (uint8_t)data[4];
    if (outPkt.flags != FLAG_DATA && outPkt.flags != FLAG_ACK && outPkt.flags != FLAG_FIN) {
        cerr << format("[RDT] Invalid packet flags: {:#x}", outPkt.flags) << endl;
        return false;
    }

    // Đọc checksum (2 byte Big-Endian → ntohs)
    uint16_t netCksum = 0;
    netCksum |= ((uint16_t)(uint8_t)data[5] << 8);
    netCksum |= ((uint16_t)(uint8_t)data[6]);
    outPkt.checksum = ntohs(netCksum);

    // Đọc payloadLength (2 byte Big-Endian → ntohs)
    uint16_t netLen = 0;
    netLen |= ((uint16_t)(uint8_t)data[7] << 8);
    netLen |= ((uint16_t)(uint8_t)data[8]);
    outPkt.payloadLength = ntohs(netLen);

    // Kiểm tra payloadLength khớp với kích thước thực nhận
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

    outPkt.payload.assign(data + RDT_HEADER_SIZE, data + length);
    return true;
}

// Giả lập mất gói tin — dùng bộ sinh ngẫu nhiên thread_local (an toàn đa luồng)
bool shouldSimulateLoss() {
    if (!SIMULATE_PACKET_LOSS) return false;

    thread_local mt19937 rng(random_device{}());
    thread_local uniform_int_distribution<int> dist(0, 99);

    return dist(rng) < LOSS_PERCENT;
}
