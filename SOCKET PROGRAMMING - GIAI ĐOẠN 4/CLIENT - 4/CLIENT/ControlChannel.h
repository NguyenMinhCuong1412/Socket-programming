#pragma once
#include "lib.h"

class ControlChannel {
private:
	unsigned short serverTcpPort; //Port TCP của Server
	string serverIp;              //IP của Server
	SOCKET tcpSocket;             //socket phục vụ kênh điều khiển - Client
public:
	ControlChannel(unsigned short, string);
	~ControlChannel() = default;

	bool start(); //Tạo socket + định danh địa chỉ + Bind + Listen
	void run();   //Accept + vòng lặp nhận lệnh/gửi phản hồi
	void stop();  //Đóng socket
};