#include "RdtPacket.h"

// ============================================================
// computeChecksum — Tính Internet checksum (1's complement sum)
//
// Thuật toán:
//   1. Cộng từng cặp 2 byte (big-endian) vào biến sum 32-bit
//   2. Nếu số byte lẻ → byte cuối được pad thêm 0x00
//   3. Fold carry: cộng phần tràn 16 bit cao vào 16 bit thấp
//   4. Đảo bit (~) kết quả → trả về
//
// LƯU Ý: Field checksum trong packet phải = 0 trước khi gọi hàm này
// ============================================================
uint16_t computeChecksum(const char* data, int length) {
    uint32_t sum = 0;

    // Cộng từng cặp 2 byte
    int i = 0;
    while (i + 1 < length) {
        // Ghép 2 byte liên tiếp thành 1 giá trị 16-bit (big-endian)
        uint16_t word = ((uint8_t)data[i] << 8) | (uint8_t)data[i + 1];
        sum += word;
        i += 2;
    }

    // Nếu số byte lẻ → byte cuối pad thêm 0x00 phía sau
    if (i < length) {
        uint16_t word = (uint8_t)data[i] << 8; // pad 0x00
        sum += word;
    }

    // Fold carry: cộng phần tràn (16 bit cao) vào 16 bit thấp
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    // Đảo bit
    return ~(uint16_t)sum;
}

// ============================================================
// verifyChecksum — Kiểm tra checksum của packet đã serialize
//
// Cách hoạt động: tính checksum trên TOÀN BỘ packet (bao gồm
// cả field checksum đã có giá trị) → nếu packet không bị lỗi
// thì kết quả phải = 0
// ============================================================
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
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    // Nếu không lỗi, tổng 1's complement phải = 0xFFFF → ~0xFFFF = 0
    return ((uint16_t)~sum == 0);
}

// ============================================================
// serializePacket — Chuyển RdtPacket thành mảng byte
//
// Thứ tự ghi:
//   [0..3]  seqNum        (4 byte, network byte order)
//   [4]     flags         (1 byte)
//   [5..6]  checksum      (2 byte, network byte order) — ghi 0 trước, tính sau
//   [7..8]  payloadLength (2 byte, network byte order)
//   [9..]   payload       (payloadLength byte)
//
// Sau khi ghi xong tất cả field (checksum=0), tính checksum
// trên toàn bộ buffer, rồi ghi ngược lại vào vị trí [5..6]
// ============================================================
std::vector<char> serializePacket(const RdtPacket& pkt) {
    int totalSize = RDT_HEADER_SIZE + (int)pkt.payload.size();
    std::vector<char> buf(totalSize, 0);

    // --- Ghi seqNum (4 byte, network byte order) ---
    uint32_t netSeq = htonl(pkt.seqNum);
    buf[0] = (char)((netSeq >> 24) & 0xFF);
    buf[1] = (char)((netSeq >> 16) & 0xFF);
    buf[2] = (char)((netSeq >> 8) & 0xFF);
    buf[3] = (char)(netSeq & 0xFF);

    // --- Ghi flags (1 byte) ---
    buf[4] = (char)pkt.flags;

    // --- Ghi checksum = 0 tạm thời (2 byte) ---
    buf[5] = 0;
    buf[6] = 0;

    // --- Ghi payloadLength (2 byte, network byte order) ---
    uint16_t netLen = htons((uint16_t)pkt.payload.size());
    buf[7] = (char)((netLen >> 8) & 0xFF);
    buf[8] = (char)(netLen & 0xFF);

    // --- Ghi payload ---
    for (int i = 0; i < (int)pkt.payload.size(); i++) {
        buf[RDT_HEADER_SIZE + i] = pkt.payload[i];
    }

    // --- Tính checksum trên toàn bộ buffer (với checksum field = 0) ---
    uint16_t cksum = computeChecksum(buf.data(), totalSize);

    // --- Ghi checksum vào đúng vị trí [5..6] (network byte order) ---
    uint16_t netCksum = htons(cksum);
    buf[5] = (char)((netCksum >> 8) & 0xFF);
    buf[6] = (char)(netCksum & 0xFF);

    return buf;
}

// ============================================================
// deserializePacket — Chuyển mảng byte thành RdtPacket
//
// Kiểm tra:
//   1. Đủ dữ liệu cho header (tối thiểu 9 byte)
//   2. payloadLength + header = tổng length nhận được
//   3. Checksum hợp lệ (verifyChecksum)
//
// Return false nếu bất kỳ kiểm tra nào thất bại
// ============================================================
bool deserializePacket(const char* data, int length, RdtPacket& outPkt) {
    // Kiểm tra đủ header
    if (length < RDT_HEADER_SIZE) {
        std::cerr << "[RDT] Packet too short: " << length << " bytes (need " << RDT_HEADER_SIZE << ")" << std::endl;
        return false;
    }

    // --- Đọc seqNum (4 byte, network byte order) ---
    uint32_t netSeq = 0;
    netSeq |= ((uint32_t)(uint8_t)data[0] << 24);
    netSeq |= ((uint32_t)(uint8_t)data[1] << 16);
    netSeq |= ((uint32_t)(uint8_t)data[2] << 8);
    netSeq |= ((uint32_t)(uint8_t)data[3]);
    outPkt.seqNum = ntohl(netSeq);

    // --- Đọc flags (1 byte) ---
    outPkt.flags = (uint8_t)data[4];

    // --- Đọc checksum (2 byte, network byte order) ---
    uint16_t netCksum = 0;
    netCksum |= ((uint16_t)(uint8_t)data[5] << 8);
    netCksum |= ((uint16_t)(uint8_t)data[6]);
    outPkt.checksum = ntohs(netCksum);

    // --- Đọc payloadLength (2 byte, network byte order) ---
    uint16_t netLen = 0;
    netLen |= ((uint16_t)(uint8_t)data[7] << 8);
    netLen |= ((uint16_t)(uint8_t)data[8]);
    outPkt.payloadLength = ntohs(netLen);

    // Kiểm tra payloadLength khớp với dữ liệu thực nhận
    if (outPkt.payloadLength != length - RDT_HEADER_SIZE) {
        std::cerr << "[RDT] Payload length mismatch: header says " << outPkt.payloadLength
            << " but received " << (length - RDT_HEADER_SIZE) << " bytes" << std::endl;
        return false;
    }

    // Kiểm tra checksum
    if (!verifyChecksum(data, length)) {
        std::cerr << "[RDT] Checksum verification failed" << std::endl;
        return false;
    }

    // --- Đọc payload ---
    outPkt.payload.assign(data + RDT_HEADER_SIZE, data + length);

    return true;
}

// ============================================================
// shouldSimulateLoss — Quyết định có "bỏ" gói này không
//
// Dùng random engine thread-local để thread-safe
// Chỉ trả về true khi SIMULATE_PACKET_LOSS = true
// ============================================================
bool shouldSimulateLoss() {
    if (!SIMULATE_PACKET_LOSS) return false;

    // thread_local: mỗi thread có random engine riêng → thread-safe
    thread_local std::mt19937 rng(std::random_device{}());
    thread_local std::uniform_int_distribution<int> dist(0, 99);

    return dist(rng) < LOSS_PERCENT;
}
