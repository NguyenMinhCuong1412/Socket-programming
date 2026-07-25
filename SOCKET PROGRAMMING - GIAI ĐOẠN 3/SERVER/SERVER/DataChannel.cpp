#include "DataChannel.h"

DataChannel::DataChannel(unsigned short port) : localPort(port), udpSocket(INVALID_SOCKET) {}

bool DataChannel::open() {
	udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (udpSocket == INVALID_SOCKET) {
		cerr << format("425 Can't open data connection (WSA error: {})", WSAGetLastError()) << endl;
		return false;
	}

	sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(localPort);

	if (bind(udpSocket, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
		cerr << format("425 Can't open data connection, bind failed (WSA error: {})", WSAGetLastError()) << endl;
		closesocket(udpSocket);
		udpSocket = INVALID_SOCKET;
		return false;
	}
	return true;
}

void DataChannel::close() {
	if (udpSocket != INVALID_SOCKET) {
		closesocket(udpSocket);
		udpSocket = INVALID_SOCKET;
	}
}

bool DataChannel::receiveFile(const string& filepath) {
	ofstream out(filepath, std::ios::binary);
	if (!out.is_open()) {
		cerr << format("550 File unavailable, cannot open '{}' for writing", filepath) << endl;
		return false;
	}

	char buffer[CHUNK_SIZE];
	sockaddr_in senderAddr;
	int senderAddrLen = sizeof(senderAddr);

	while (true) {
		int bytesReceived = recvfrom(udpSocket, buffer, CHUNK_SIZE, 0,
			(sockaddr*)&senderAddr, &senderAddrLen);

		if (bytesReceived == SOCKET_ERROR) {
			cerr << format("426 Connection closed, transfer aborted (WSA error: {})", WSAGetLastError()) << endl;
			out.close();
			return false;
		}
		if (bytesReceived == 0) break; // gói rỗng = EOF

		out.write(buffer, bytesReceived);
	}

	out.close();
	return true;
}

bool DataChannel::sendFile(const string& filepath, const string& destIp, unsigned short destPort) {
	ifstream in(filepath, std::ios::binary);
	if (!in.is_open()) {
		cerr << format("550 File unavailable, cannot open '{}' for reading", filepath) << endl;
		return false;
	}

	sockaddr_in destAddr;
	destAddr.sin_family = AF_INET;
	destAddr.sin_port = htons(destPort);
	inet_pton(AF_INET, destIp.c_str(), &destAddr.sin_addr);

	char buffer[CHUNK_SIZE];
	while (in.read(buffer, CHUNK_SIZE) || in.gcount() > 0) {
		int bytesToSend = (int)in.gcount();
		sendto(udpSocket, buffer, bytesToSend, 0, (sockaddr*)&destAddr, sizeof(destAddr));
	}

	// gói rỗng báo EOF
	sendto(udpSocket, "", 0, 0, (sockaddr*)&destAddr, sizeof(destAddr));

	in.close();
	return true;
}

DataChannel::~DataChannel() { close(); }