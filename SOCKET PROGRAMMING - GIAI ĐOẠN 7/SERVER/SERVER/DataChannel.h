#pragma once
#include "lib.h"
#include "RdtPacket.h"

class DataChannel {
private:
	unsigned short udpPort;
	atomic<SOCKET> udpSocket;

	bool rdtSend(SOCKET s, const char* data, size_t len, const sockaddr_in& dest);
	bool rdtSend(SOCKET s, std::ifstream& in, uintmax_t len, const sockaddr_in& dest);
	int rdtReceive(SOCKET s, vector<char>& outData, sockaddr_in& senderAddr);
public:
	DataChannel(unsigned short);
	~DataChannel() = default;

	bool start();
	unsigned short getBoundPort() const;
	bool receiveFile(const string&, bool = false, bool = false);
	bool sendFile(const string&, const string&, unsigned short, bool = false);

	bool sendFileAfterHandshake(const string&, bool = false);

	bool sendProbe(const string&, unsigned short);

	void stop();
};