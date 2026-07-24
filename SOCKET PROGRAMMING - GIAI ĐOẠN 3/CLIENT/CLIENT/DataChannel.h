#pragma once
#include "lib.h"

// Kênh dữ liệu UDP cố định (Basic Level).
// LƯU Ý: sendto/recvfrom thô, CHƯA có RDT — sẽ thay bằng tầng RDT ở Giai đoạn 6.
// Giao thức kết thúc file đơn giản: gửi các chunk CHUNK_SIZE liên tiếp,
// gói tin rỗng (0 byte) báo hiệu EOF.
class DataChannel {
private:
	SOCKET udpSocket;
	unsigned short localPort;
public:
	DataChannel(unsigned short port);

	bool open();
	void close();

	// Server dùng khi nhận STOR: bind cổng cố định, chờ dữ liệu từ client
	bool receiveFile(const string& filepath);

	// Server dùng khi gửi RETR: gửi file tới địa chỉ đích qua UDP
	bool sendFile(const string& filepath, const string& destIp, unsigned short destPort);

	~DataChannel();
};