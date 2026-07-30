#pragma once
#include "lib.h"

//Khởi tạo kênh điều khiển - TCP
class ControlChannel {
private:
	unsigned short tcpPort; //Port để phục vụ kênh điều khiển
	SOCKET tcpSocket;       //socket phục vụ kênh điều khiển - Server

	// Logic xử lý 1 client (trước đây nằm trong run()) - giờ chạy trong 1 thread riêng,
	// cho phép nhiều client kết nối và thao tác đồng thời, không cần đợi nhau.
	void handleClient(SOCKET clientSocket, string clientIp);
public:
	ControlChannel(unsigned short);
	~ControlChannel() = default;

	bool start();  //Tạo socket + định danh địa chỉ + Bind + Listen
	void run();    //Vòng lặp accept() VÔ HẠN - mỗi client mới -> 1 std::thread riêng (detach)
	void stop();   //Đóng socket
};