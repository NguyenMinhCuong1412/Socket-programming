// ======================================================================
// Server.cpp — ĐIỂM KHỞI CHẠY (ENTRY POINT) CỦA CHƯƠNG TRÌNH FTP SERVER
//    Khởi tạo Winsock, tạo thư mục gốc Server, khởi chạy kênh điều khiển
//    TCP lắng nghe kết nối từ Client, rồi chạy vòng lặp accept().
// ======================================================================
#include "lib.h"
#include "ControlChannel.h"

int main() {
	// Khởi tạo Winsock 2.2 — bắt buộc trước khi sử dụng socket trên Windows
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		cerr << "421 Service not available, WSAStartup failed" << endl;  // 421: dịch vụ không khả dụng
		return 1;
	}

	// Tạo thư mục gốc Server (server_root) — nơi lưu trữ tất cả file FTP
	error_code ec;
	fs::create_directories(SERVER_ROOT, ec);
	if (ec) {
		cerr << format("421 Service not available, cannot create server root '{}': {}", SERVER_ROOT.string(), ec.message()) << endl;
		WSACleanup();
		return 1;
	}
	cout << "Server root: " << SERVER_ROOT.string() << endl;

	// Khởi tạo kênh điều khiển TCP trên CONTROL_PORT (8080)
	ControlChannel control(CONTROL_PORT);
	if (!control.start()) {
		WSACleanup();
		return 1;
	}
	// Chạy vòng lặp accept() — mỗi Client kết nối → thread riêng
	control.run();
	// Dọn dẹp khi thoát
	control.stop();

	// Giải phóng tài nguyên Winsock
	WSACleanup();
	return 0;
}