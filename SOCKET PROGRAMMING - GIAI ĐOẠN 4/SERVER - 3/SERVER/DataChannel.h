#pragma once
#include "lib.h"

class DataChannel {
private:
	unsigned short udpPort; //Port phục vụ cho kênh dữ liệu
	SOCKET udpSocket;       //socket phục vụ cho kênh dữ liệu
public:
	DataChannel(unsigned short);
	~DataChannel() = default;

	bool start();

	//append=false: ghi đè (dùng cho STOR/STOU) | append=true: nối vào cuối file (dùng cho APPE)
	bool receiveFile(const string&, bool append = false);
	bool sendFile(const string&, const string&, unsigned short);

	void stop();
};