// ======================================================================
// DataChannel.h — KÊNH DỮ LIỆU (UDP) CỦA SERVER, TRUYỀN FILE BẰNG RDT
//    Sử dụng giao thức UDP kết hợp cơ chế RDT (RdtPacket) để truyền
//    file tin cậy giữa Server và Client, hỗ trợ cả 2 chiều gửi/nhận.
//    Khác với Client: Server không có thanh tiến trình và rdtSend/rdtReceive
//    không có tham số totalSize (vì Server không cần hiển thị tiến trình).
// ======================================================================
#pragma once
#include "lib.h"
#include "RdtPacket.h"

class DataChannel {
private:
	unsigned short udpPort;    // Cổng UDP sẽ bind (dùng port cố định trong Passive mode, 0 trong Active)
	atomic<SOCKET> udpSocket;  // Socket UDP — atomic để stop() có thể đóng an toàn từ thread khác (ABOR)

	// Gửi dữ liệu tin cậy bằng RDT Go-Back-N — phiên bản đọc từ buffer (dữ liệu nhỏ)
	bool rdtSend(SOCKET s, const char* data, size_t len, const sockaddr_in& dest);
	// Gửi dữ liệu tin cậy bằng RDT Go-Back-N — phiên bản đọc từ file stream (file lớn)
	bool rdtSend(SOCKET s, std::ifstream& in, uintmax_t len, const sockaddr_in& dest);
	// Nhận dữ liệu tin cậy bằng RDT Go-Back-N — trả về tổng byte nhận, -1 nếu lỗi
	int rdtReceive(SOCKET s, vector<char>& outData, sockaddr_in& senderAddr);
public:
	DataChannel(unsigned short); // Khởi tạo với port dự kiến bind
	~DataChannel() = default;

	bool start();                // Tạo socket UDP, bind vào udpPort
	unsigned short getBoundPort() const; // Lấy port thực tế đã bind
	// Nhận file qua RDT, ghi vào filepath. append: nối thêm, isAscii: chế độ text
	bool receiveFile(const string&, bool = false, bool = false);
	// Gửi file đến destIp:destPort qua RDT
	bool sendFile(const string&, const string&, unsigned short, bool = false);

	// Gửi file trong Passive mode: chờ probe từ Client → học địa chỉ → gửi file
	bool sendFileAfterHandshake(const string&, bool = false);

	// Gửi gói probe nhỏ (1 byte 'R') — dùng trong Active mode
	bool sendProbe(const string&, unsigned short);

	// Đóng socket UDP — an toàn gọi từ thread khác nhờ atomic exchange
	void stop();
};