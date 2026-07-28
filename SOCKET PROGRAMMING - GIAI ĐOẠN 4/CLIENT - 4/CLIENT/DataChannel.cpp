#include "DataChannel.h"

DataChannel::DataChannel(unsigned short port) {
	this->udpPort = port;
	this->udpSocket = INVALID_SOCKET;
}

bool DataChannel::start() {
	//Tạo socket UDP
	this->udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (this->udpSocket == INVALID_SOCKET) {
		cerr << format("425 Can't open data connection, cannot create socket (WSA error: {})", WSAGetLastError()) << endl;
		return false;
	}

	//Định danh địa chỉ 
	sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(this->udpPort);

	//Bind socket
	if (bind(this->udpSocket, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
		cerr << format("425 Can't open data connection, bind failed (WSA error: {})", WSAGetLastError()) << endl;
		closesocket(this->udpSocket);
		this->udpSocket = INVALID_SOCKET;
		return false;
	}

	return true;
}

bool DataChannel::receiveFile(const string& filepath, bool append) {
	std::ios::openmode mode = ios::binary | (append ? ios::app : ios::trunc);
	ofstream out(filepath, mode);
	if (!out.is_open()) {
		cerr << format("550 File unavailable, cannot open '{}' for writing", filepath) << endl;
		return false;
	}

	char buffer[CHUNK_SIZE];
	sockaddr_in senderAddr;
	int senderAddrLen = sizeof(senderAddr);

	while (true) {
		int byteRecv = recvfrom(this->udpSocket, buffer, CHUNK_SIZE, 0, (sockaddr*)&senderAddr, &senderAddrLen);

		if (byteRecv == SOCKET_ERROR) {
			cerr << format("426 Connection closed, transfer aborted (WSA error: {})", WSAGetLastError()) << endl;
			out.close();
			return false;
		}
		if (byteRecv == 0) break;  //EOF 

		out.write(buffer, byteRecv);
	}

	out.close();
	return true;
}

bool DataChannel::sendFile(const string& filepath, const string& destIp, unsigned short destPort) {
	ifstream in(filepath, ios::binary);
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
		sendto(this->udpSocket, buffer, bytesToSend, 0, (sockaddr*)&destAddr, sizeof(destAddr));
	}

	//EOF
	sendto(this->udpSocket, "", 0, 0, (sockaddr*)&destAddr, sizeof(destAddr));

	in.close();
	return true;
}

void DataChannel::stop() {
	if (this->udpSocket != INVALID_SOCKET) {
		closesocket(this->udpSocket);
		this->udpSocket = INVALID_SOCKET;
	}
}