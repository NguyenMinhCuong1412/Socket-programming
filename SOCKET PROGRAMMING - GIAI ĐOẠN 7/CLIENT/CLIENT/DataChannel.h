#pragma once
#include "lib.h"
#include "RdtPacket.h"

//Khởi tạo kênh dữ liệu - UDP
class DataChannel {
private:
	unsigned short udpPort;   //Port để phục vụ kênh dữ liệu
	atomic<SOCKET> udpSocket; //Socket phục vụ kênh dữ liệu của Server-UDP, đảm bảo stop() có thể đóng socket an toàn

	bool rdtSend(SOCKET s, const char* data, size_t len, const sockaddr_in& dest);
	int rdtReceive(SOCKET s, std::vector<char>& outData, sockaddr_in& senderAddr);
public:
	DataChannel(unsigned short);
	~DataChannel() = default;

	bool start();
	unsigned short getBoundPort() const; //Trả về cổng UDP thực tế đã bind (dùng khi bind với port=0 để OS tự cấp phát ngẫu nhiên)
	bool receiveFile(const string&, bool = false, bool = false);
	bool sendFile(const string&, const string&, unsigned short, bool = false);

	//PASSIVE + RETR: server chưa biết địa chỉ Client, chờ nhận 1 gói "probe" nhỏ để học địa chỉ, rồi mới gửi file tới đúng địa chỉ đó
	bool sendFileAfterHandshake(const string&, bool = false);

	//Phía Client khi RETR trong chế độ PASSIVE: gửi 1 gói tin nhỏ để server học địa chỉ IP:port
	bool sendProbe(const string&, unsigned short);

	void stop();
};