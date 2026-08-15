// ======================================================================
// DataChannel.h — KÊNH DỮ LIỆU (UDP) CỦA CLIENT, TRUYỀN FILE BẰNG RDT
//    Sử dụng giao thức UDP kết hợp cơ chế RDT (RdtPacket) để truyền
//    file tin cậy giữa Client và Server, hỗ trợ cả 2 chiều gửi/nhận,
//    tương tự kênh dữ liệu (Data Connection) của giao thức FTP.
//    Hỗ trợ cả chế độ Active (Client mở port) và Passive (Client kết
//    nối đến port của Server), kèm thanh tiến trình hiển thị trên console.
// ======================================================================
#pragma once
#include "lib.h"
#include "RdtPacket.h"

class DataChannel {
private:
	unsigned short udpPort;    // Cổng UDP sẽ bind (0 = để OS tự chọn port ngẫu nhiên, dùng trong Passive mode)
	atomic<SOCKET> udpSocket;  // Socket UDP — kiểu atomic để có thể đóng an toàn từ thread khác (stop())

	// Gửi dữ liệu tin cậy qua UDP bằng RDT Go-Back-N — phiên bản đọc từ buffer (dữ liệu nhỏ, đã có sẵn trong bộ nhớ)
	// totalSize: kích thước tổng để hiển thị thanh tiến trình (0 = không hiển thị)
	bool rdtSend(SOCKET s, const char* data, size_t len, const sockaddr_in& dest, uintmax_t totalSize = 0);
	// Gửi dữ liệu tin cậy qua UDP bằng RDT Go-Back-N — phiên bản đọc từ file stream (file lớn, đọc theo chunk)
	bool rdtSend(SOCKET s, std::ifstream& in, uintmax_t len, const sockaddr_in& dest, uintmax_t totalSize = 0);
	// Nhận dữ liệu tin cậy qua UDP bằng RDT Go-Back-N — trả về tổng byte đã nhận, -1 nếu lỗi
	int rdtReceive(SOCKET s, std::vector<char>& outData, sockaddr_in& senderAddr, uintmax_t totalSize = 0);
public:
	DataChannel(unsigned short); // Khởi tạo với port dự kiến bind (0 = tự chọn)
	~DataChannel() = default;

	bool start();                // Tạo socket UDP, bind vào udpPort
	unsigned short getBoundPort() const; // Lấy port thực tế đã bind (dùng khi port=0 để OS tự chọn)
	// Nhận file từ đầu bên kia qua RDT, ghi vào filepath
	// totalSize: kích thước dự kiến (để hiển thị tiến trình), append: nối thêm vào file, isAscii: chế độ text
	bool receiveFile(const string&, uintmax_t = 0, bool = false, bool = false);
	// Gửi file đến địa chỉ destIp:destPort qua RDT
	bool sendFile(const string&, const string&, unsigned short, uintmax_t = 0, bool = false);

	// Gửi file sau khi nhận probe từ đầu bên kia (dùng trong Passive mode — Server chờ Client gửi probe trước)
	bool sendFileAfterHandshake(const string&, uintmax_t = 0, bool = false);

	// Gửi gói probe nhỏ (1 byte 'R') để báo cho Server biết địa chỉ Client (dùng trong Passive mode)
	bool sendProbe(const string&, unsigned short);

	// Đóng socket UDP, dừng kênh dữ liệu — an toàn gọi từ thread khác nhờ atomic exchange
	void stop();
};