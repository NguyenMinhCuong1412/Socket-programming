#pragma once
#include "lib.h"

//Khởi tạo kênh điều khiển - TCP
class ControlChannel {
private:
	unsigned short tcpPort; //Port để phục vụ kênh điều khiển
	SOCKET tcpSocket;       //socket phục vụ kênh điều khiển - Server
public:
	ControlChannel(unsigned short);
	~ControlChannel() = default;

	bool start(); //Tạo socket + định danh địa chỉ + Bind + Listen
	void run();   //Accept + vòng lặp nhận lệnh/gửi phản hồi
	void stop();  //Đóng socket
};