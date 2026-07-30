#pragma once
#include "lib.h"

//Khởi tạo kênh điều khiển - TCP
class ControlChannel {
private:
	unsigned short tcpPort; //Port để phục vụ kênh điều khiển
	SOCKET tcpSocket;       //socket phục vụ kênh điều khiển của Server-TCP

	//Quản lý toàn bộ vòng đời của Kênh điều khiển cho một kết nối FTP từ Client-TCP
	void handleClient(SOCKET clientSocket, string clientIp);
public:
	ControlChannel(unsigned short); //Constructor - tham số tcpPort
	~ControlChannel() = default;    //Destructor mặc định

	bool start();  //Tạo socket + định danh địa chỉ + Bind + Listen
	void run();    //Vòng lặp accept() VÔ HẠN - mỗi client mới -> 1 std::thread riêng (detach)
	void stop();   //Đóng socket
};