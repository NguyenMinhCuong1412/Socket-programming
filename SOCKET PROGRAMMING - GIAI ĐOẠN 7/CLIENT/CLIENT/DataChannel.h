#pragma once
#include "lib.h"
#include "RdtPacket.h"

class DataChannel {
private:
	unsigned short udpPort;
	atomic<SOCKET> udpSocket;

	bool rdtSend(SOCKET s, const char* data, size_t len, const sockaddr_in& dest, uintmax_t totalSize = 0);
	bool rdtSend(SOCKET s, std::ifstream& in, uintmax_t len, const sockaddr_in& dest, uintmax_t totalSize = 0);
	int rdtReceive(SOCKET s, std::vector<char>& outData, sockaddr_in& senderAddr, uintmax_t totalSize = 0);
public:
	DataChannel(unsigned short);
	~DataChannel() = default;

	bool start();
	unsigned short getBoundPort() const;
	bool receiveFile(const string&, uintmax_t = 0, bool = false, bool = false);
	bool sendFile(const string&, const string&, unsigned short, uintmax_t = 0, bool = false);

	bool sendFileAfterHandshake(const string&, uintmax_t = 0, bool = false);

	bool sendProbe(const string&, unsigned short);

	void stop();
};