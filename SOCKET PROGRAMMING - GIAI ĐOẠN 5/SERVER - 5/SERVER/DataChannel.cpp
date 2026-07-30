#include "DataChannel.h"

DataChannel::DataChannel(unsigned short port) {
	this->udpPort = port;
	this->udpSocket.store(INVALID_SOCKET);
}

bool DataChannel::start() {
	SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (s == INVALID_SOCKET) {
		cerr << format("425 Can't open data connection, cannot create socket (WSA error: {})", WSAGetLastError()) << endl;
		return false;
	}

	sockaddr_in serverAddrUdp;
	serverAddrUdp.sin_family = AF_INET;
	serverAddrUdp.sin_addr.s_addr = INADDR_ANY;
	serverAddrUdp.sin_port = htons(this->udpPort);

	if (bind(s, (sockaddr*)&serverAddrUdp, sizeof(serverAddrUdp)) == SOCKET_ERROR) {
		cerr << format("425 Can't open data connection, bind failed (WSA error: {})", WSAGetLastError()) << endl;
		closesocket(s);
		return false;
	}

	this->udpSocket.store(s);
	return true;
}

bool DataChannel::receiveFile(const string& filepath, bool append) {
	ios::openmode mode = ios::binary | (append ? ios::app : ios::trunc);
	ofstream out(filepath, mode);
	if (!out.is_open()) {
		cerr << format("550 File unavailable, cannot open '{}' for writing", filepath) << endl;
		return false;
	}

	char buffer[CHUNK_SIZE];
	sockaddr_in senderAddr;
	int senderAddrLen = sizeof(senderAddr);

	while (true) {
		SOCKET s = udpSocket.load();
		if (s == INVALID_SOCKET) { out.close(); return false; } // đã bị ABOR đóng từ trước

		int byteRecv = recvfrom(s, buffer, CHUNK_SIZE, 0, (sockaddr*)&senderAddr, &senderAddrLen);
		if (byteRecv == 0) break;
		if (byteRecv == SOCKET_ERROR) { // có thể do lỗi mạng thật, hoặc do ABOR gọi stop() làm recvfrom() thoát ra
			cerr << format("426 Connection closed, transfer aborted (WSA error: {})", WSAGetLastError()) << endl;
			out.close();
			return false;
		}
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
		SOCKET s = udpSocket.load();
		if (s == INVALID_SOCKET) { in.close(); return false; } // đã bị ABOR đóng

		int byteToSend = (int)in.gcount();
		if (sendto(s, buffer, byteToSend, 0, (sockaddr*)&destAddr, sizeof(destAddr)) == SOCKET_ERROR) {
			in.close();
			return false;
		}
	}

	SOCKET s = udpSocket.load();
	if (s != INVALID_SOCKET) sendto(s, "", 0, 0, (sockaddr*)&destAddr, sizeof(destAddr));

	in.close();
	return true;
}

bool DataChannel::sendFileAfterHandshake(const string& filepath) {
	char probe[16];
	sockaddr_in clientAddr;
	int clientAddrLen = sizeof(clientAddr);

	SOCKET s = udpSocket.load();
	if (s == INVALID_SOCKET) return false;

	int n = recvfrom(s, probe, sizeof(probe), 0, (sockaddr*)&clientAddr, &clientAddrLen);
	if (n == SOCKET_ERROR) {
		cerr << format("426 Connection closed, transfer aborted (WSA error: {})", WSAGetLastError()) << endl;
		return false;
	}

	char ipStr[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, INET_ADDRSTRLEN);
	unsigned short learnedPort = ntohs(clientAddr.sin_port);

	return sendFile(filepath, ipStr, learnedPort);
}

bool DataChannel::sendProbe(const string& destIp, unsigned short destPort) {
	SOCKET s = udpSocket.load();
	if (s == INVALID_SOCKET) return false;

	sockaddr_in destAddr;
	destAddr.sin_family = AF_INET;
	destAddr.sin_port = htons(destPort);
	inet_pton(AF_INET, destIp.c_str(), &destAddr.sin_addr);

	return sendto(s, "R", 1, 0, (sockaddr*)&destAddr, sizeof(destAddr)) != SOCKET_ERROR;
}

void DataChannel::stop() {
	SOCKET s = udpSocket.exchange(INVALID_SOCKET); // atomic swap: chỉ 1 thread thực sự đóng
	if (s != INVALID_SOCKET) closesocket(s);
}