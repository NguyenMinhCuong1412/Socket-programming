#pragma once
#include "lib.h"

//Khởi tạo kênh điều khiển - TCP
class ControlChannel {
private:
	unsigned short tcpPort; //Port để phục vụ kênh điều khiển
	SOCKET tcpSocket;       //socket phục vụ kênh điều khiển của Server-TCP

	void handleClient(SOCKET clientSocket, string clientIp); //Quản lý toàn bộ vòng đời của kênh điều khiển cho một kết nối FTP từ Client-TCP
public:
	ControlChannel(unsigned short); 
	~ControlChannel() = default;   

	bool start();  //Tạo socket + định danh địa chỉ + Bind + Listen
	void run();    //Vòng lặp accept() VÔ HẠN - mỗi client mới -> 1 std::thread riêng (detach)
	void stop();   //Đóng socket
};