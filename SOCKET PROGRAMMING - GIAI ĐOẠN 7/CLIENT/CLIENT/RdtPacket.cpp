#include "RdtPacket.h"

uint16_t computeChecksum(const char* data, int length) {
    uint32_t sum = 0;

    //Cộng từng cặp 2 byte
    int i = 0;
    while (i + 1 < length) {
        //Ghép 2 byte liên tiếp thành 1 giá trị 16-bit (big-endian)
        uint16_t word = ((uint8_t)data[i] << 8) | (uint8_t)data[i + 1]; //<< 8: dịch trái byte thứ 1 sang 8 bit; |: phép OR gom byte thứ 2 vào chung byte thứ 1 -> kết quả: số 16 bit
        sum += word;                                                    //Cộng dồn vào sum
        i += 2;
    }

    //Nếu số byte lẻ -> byte cuối pad thêm 0x00 phía sau
    if (i < length) {
        uint16_t word = (uint8_t)data[i] << 8; //pad 0x00
        sum += word;
    }

    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16); //Fold carry: cộng phần tràn 16 bit thấp + 16 bit cao
    return ~(uint16_t)sum; //Đảo bit
}

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
    return ((uint16_t)~sum == 0); // Nếu không lỗi, tổng 1's complement phải = 0xFFFF → ~0xFFFF = 0
}

vector<char> serializePacket(const RdtPacket& pkt) {
    int totalSize = RDT_HEADER_SIZE + (int)pkt.payload.size();
    vector<char> buf(totalSize, 0);

    //Ghi seqNum (4 byte, network byte order)
    uint32_t netSeq = htonl(pkt.seqNum);
    buf[0] = (char)((netSeq >> 24) & 0xFF);
    buf[1] = (char)((netSeq >> 16) & 0xFF);
    buf[2] = (char)((netSeq >> 8) & 0xFF);
    buf[3] = (char)(netSeq & 0xFF);

    buf[4] = (char)pkt.flags; //Ghi flags (1 byte)
    buf[5] = 0; buf[6] = 0; //Ghi checksum = 0 tạm thời (2 byte)

    //Ghi payloadLength (2 byte, network byte order)
    uint16_t netLen = htons((uint16_t)pkt.payload.size());
    buf[7] = (char)((netLen >> 8) & 0xFF);
    buf[8] = (char)(netLen & 0xFF);

    for (int i = 0; i < (int)pkt.payload.size(); i++) buf[RDT_HEADER_SIZE + i] = pkt.payload[i]; //Ghi payload
    uint16_t cksum = computeChecksum(buf.data(), totalSize); //Tính checksum trên toàn bộ buffer (với checksum field = 0)

    //Ghi checksum vào đúng vị trí [5..6] (network byte order)
    uint16_t netCksum = htons(cksum);
    buf[5] = (char)((netCksum >> 8) & 0xFF);
    buf[6] = (char)(netCksum & 0xFF);

    return buf;
}

bool deserializePacket(const char* data, int length, RdtPacket& outPkt) {
    //Kiểm tra đủ header
    if (length < RDT_HEADER_SIZE) {
        cerr << format("[RDT] Packet too short: {} bytes (need {})", length, RDT_HEADER_SIZE) << endl;
        return false;
    }

    //Đọc seqNum (4 byte, network byte order)
    uint32_t netSeq = 0;
    netSeq |= ((uint32_t)(uint8_t)data[0] << 24);
    netSeq |= ((uint32_t)(uint8_t)data[1] << 16);
    netSeq |= ((uint32_t)(uint8_t)data[2] << 8);
    netSeq |= ((uint32_t)(uint8_t)data[3]);
    outPkt.seqNum = ntohl(netSeq);

    outPkt.flags = (uint8_t)data[4]; //Đọc flags (1 byte)
    if (outPkt.flags != FLAG_DATA && outPkt.flags != FLAG_ACK && outPkt.flags != FLAG_FIN) {
        cerr << format("[RDT] Invalid packet flags: {:#x}", outPkt.flags) << endl;
        return false;
    }

    //Đọc checksum (2 byte, network byte order)
    uint16_t netCksum = 0;
    netCksum |= ((uint16_t)(uint8_t)data[5] << 8);
    netCksum |= ((uint16_t)(uint8_t)data[6]);
    outPkt.checksum = ntohs(netCksum);

    //Đọc payloadLength (2 byte, network byte order)
    uint16_t netLen = 0;
    netLen |= ((uint16_t)(uint8_t)data[7] << 8);
    netLen |= ((uint16_t)(uint8_t)data[8]);
    outPkt.payloadLength = ntohs(netLen);

    //Kiểm tra payloadLength khớp với dữ liệu thực nhận
    if (outPkt.payloadLength != length - RDT_HEADER_SIZE) {
        cerr << format("[RDT] Payload length mismatch: header says {} but received {} bytes",
            outPkt.payloadLength, (length - RDT_HEADER_SIZE)) << endl;
        return false;
    }

    //Kiểm tra checksum
    if (!verifyChecksum(data, length)) {
        cerr << "[RDT] Checksum verification failed" << endl;
        return false;
    }

    outPkt.payload.assign(data + RDT_HEADER_SIZE, data + length); //Đọc payload
    return true;
}

bool shouldSimulateLoss() {
    if (!SIMULATE_PACKET_LOSS) return false;

    // thread_local: mỗi thread có random engine riêng → thread-safe
    thread_local mt19937 rng(random_device{}());
    thread_local uniform_int_distribution<int> dist(0, 99);

    return dist(rng) < LOSS_PERCENT;
}
