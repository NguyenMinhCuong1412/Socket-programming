#pragma once
#include "lib.h"

//Khởi tạo kênh dữ liệu - UDP
class DataChannel {
private:
	unsigned short udpPort;   //Port để phục vụ kênh dữ liệu
	atomic<SOCKET> udpSocket; //Socket phục vụ kênh dữ liệu của Server-UDP, đảm bảo stop() có thể đóng socket an toàn
public:
	DataChannel(unsigned short);
	~DataChannel() = default;

	bool start();
	bool receiveFile(const string&, bool = false);
	bool sendFile(const string&, const string&, unsigned short);

	//PASSIVE + RETR: server chưa biết địa chỉ Client, chờ nhận 1 gói "probe" nhỏ để học địa chỉ, rồi mới gửi file tới đúng địa chỉ đó
	bool sendFileAfterHandshake(const string&);

	//Phía Client khi RETR trong chế độ PASSIVE: gửi 1 gói tin nhỏ để server học địa chỉ IP:port
	bool sendProbe(const string&, unsigned short);

	void stop();
};