#pragma once
#include "lib.h"

class DataChannel {
private:
	unsigned short udpPort;
	std::atomic<SOCKET> udpSocket; // atomic: stop() (thread khác, do ABOR) có thể đóng socket an toàn
	// trong khi start()/receiveFile()/sendFile() đang chạy ở thread transfer
public:
	DataChannel(unsigned short);
	~DataChannel() = default;

	bool start();
	bool receiveFile(const string&, bool = false);
	bool sendFile(const string&, const string&, unsigned short);

	// PASSIVE + RETR: server chưa biết địa chỉ client, chờ nhận 1 gói "probe" nhỏ để học địa chỉ,
	// rồi mới gửi file tới đúng địa chỉ đó.
	bool sendFileAfterHandshake(const string& filepath);

	// Phía CLIENT khi RETR trong chế độ PASSIVE: gửi 1 gói tin nhỏ để server học địa chỉ IP:port.
	bool sendProbe(const string& destIp, unsigned short destPort);

	void stop();
};