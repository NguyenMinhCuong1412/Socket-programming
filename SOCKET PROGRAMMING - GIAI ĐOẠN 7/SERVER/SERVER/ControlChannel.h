// ======================================================================
// ControlChannel.h — KÊNH ĐIỀU KHIỂN (TCP) CỦA SERVER
//    Lắng nghe kết nối TCP từ Client, tạo thread riêng cho mỗi Client,
//    và cung cấp giao diện admin console cho người quản trị Server.
//    Mỗi Client kết nối được xử lý bởi handleClient() trong thread riêng.
// ======================================================================
#pragma once
#include "lib.h"

class ControlChannel {
private:
	unsigned short tcpPort;  // Cổng TCP Server lắng nghe (CONTROL_PORT = 8080)
	SOCKET tcpSocket;        // Socket TCP lắng nghe (listen socket)

	// Xử lý một Client kết nối — chạy trong thread riêng cho mỗi Client
	// Nhận lệnh FTP liên tục → gọi CommandHandler → gửi phản hồi
	void handleClient(SOCKET clientSocket, string clientIp);
	// Vòng lặp admin console — đọc lệnh từ stdin Server (sessions, help...)
	void adminConsoleLoop();
public:
	ControlChannel(unsigned short);    // Khởi tạo với port lắng nghe
	~ControlChannel() = default;

	bool start();  // Tạo socket, bind, listen
	void run();    // Vòng lặp accept() — mỗi Client mới → tạo thread
	void stop();   // Đóng listen socket
};