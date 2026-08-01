#include "DataChannel.h"

DataChannel::DataChannel(unsigned short port) {
	this->udpPort = port;
	this->udpSocket.store(INVALID_SOCKET);
}

bool DataChannel::start() {
	//Tạo UDP-socket
	SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (s == INVALID_SOCKET) {
		cerr << format("425 Can't open data connection, cannot create socket (WSA error: {})", WSAGetLastError()) << endl;
		return false;
	}

	//Định danh địa chỉ Server-UDP
	sockaddr_in serverAddrUdp;
	serverAddrUdp.sin_family = AF_INET;
	serverAddrUdp.sin_addr.s_addr = INADDR_ANY;
	serverAddrUdp.sin_port = htons(this->udpPort);

	//Bind UDP-socket với địa chỉ Server-UDP
	if (bind(s, (sockaddr*)&serverAddrUdp, sizeof(serverAddrUdp)) == SOCKET_ERROR) {
		cerr << format("425 Can't open data connection, bind failed (WSA error: {})", WSAGetLastError()) << endl;
		closesocket(s);
		return false;
	}

	//Lưu socket vào atomic variable để các thread khác có thể truy cập và đóng an toàn
	this->udpSocket.store(s);
	return true;
}

bool DataChannel::receiveFile(const string& filepath, bool append) {
	//Mở file để ghi dữ liệu nhận được từ Client
	ios::openmode mode = ios::binary | (append ? ios::app : ios::trunc);
	ofstream out(filepath, mode);
	if (!out.is_open()) {
		cerr << format("550 File unavailable, cannot open '{}' for writing", filepath) << endl;
		return false;
	}

	//Chuẩn bị buffer để nhận dữ liệu từ Client
	char buffer[CHUNK_SIZE];
	sockaddr_in senderAddr;
	int senderAddrLen = sizeof(senderAddr);

	//Nhận dữ liệu từ Client qua UDP-socket, ghi vào file
	while (true) {
		SOCKET s = udpSocket.load();
		if (s == INVALID_SOCKET) { out.close(); return false; } 

		int byteRecv = recvfrom(s, buffer, CHUNK_SIZE, 0, (sockaddr*)&senderAddr, &senderAddrLen);
		if (byteRecv == 0) break;
		if (byteRecv == SOCKET_ERROR) { //Có thể do lỗi mạng thật, hoặc do ABOR gọi stop() làm recvfrom() thoát ra
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
	//Mở file để đọc dữ liệu gửi tới Client
	ifstream in(filepath, ios::binary);
	if (!in.is_open()) {
		cerr << format("550 File unavailable, cannot open '{}' for reading", filepath) << endl;
		return false;
	}

	//Chuẩn bị địa chỉ đích (Client) để gửi dữ liệu qua UDP
	sockaddr_in destAddr;
	destAddr.sin_family = AF_INET;
	destAddr.sin_port = htons(destPort);
	inet_pton(AF_INET, destIp.c_str(), &destAddr.sin_addr);

	//Gửi dữ liệu từ file tới Client qua UDP-socket
	char buffer[CHUNK_SIZE];
	while (in.read(buffer, CHUNK_SIZE) || in.gcount() > 0) {
		SOCKET s = udpSocket.load();
		if (s == INVALID_SOCKET) { in.close(); return false; } 

		int byteToSend = (int)in.gcount();
		if (sendto(s, buffer, byteToSend, 0, (sockaddr*)&destAddr, sizeof(destAddr)) == SOCKET_ERROR) {
			in.close();
			return false;
		}
	}

	//Gửi gói tin rỗng để báo cho Client biết đã hết dữ liệu
	SOCKET s = udpSocket.load();
	if (s != INVALID_SOCKET) sendto(s, "", 0, 0, (sockaddr*)&destAddr, sizeof(destAddr));

	in.close();
	return true;
}

bool DataChannel::sendFileAfterHandshake(const string& filepath) {
	//Chuẩn bị buffer để nhận gói tin "probe" từ Client
	char probe[16];
	sockaddr_in clientAddr;
	int clientAddrLen = sizeof(clientAddr);

	//Lấy socket UDP hiện tại
	SOCKET s = udpSocket.load();
	if (s == INVALID_SOCKET) return false;

	//Chờ nhận 1 gói tin nhỏ từ Client để học địa chỉ IP:port của Client
	int n = recvfrom(s, probe, sizeof(probe), 0, (sockaddr*)&clientAddr, &clientAddrLen);
	if (n == SOCKET_ERROR) {
		cerr << format("426 Connection closed, transfer aborted (WSA error: {})", WSAGetLastError()) << endl;
		return false;
	}

	//Chuyển đổi địa chỉ IP từ dạng nhị phân sang dạng chuỗi để sử dụng trong sendFile()
	char ipStr[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, INET_ADDRSTRLEN);
	unsigned short learnedPort = ntohs(clientAddr.sin_port);

	return sendFile(filepath, ipStr, learnedPort);
}

bool DataChannel::sendProbe(const string& destIp, unsigned short destPort) {
	//Lấy socket UDP hiện tại
	SOCKET s = udpSocket.load();
	if (s == INVALID_SOCKET) return false;

	//Chuẩn bị địa chỉ đích (Server) để gửi gói tin "probe" qua UDP
	sockaddr_in destAddr;
	destAddr.sin_family = AF_INET;
	destAddr.sin_port = htons(destPort);
	inet_pton(AF_INET, destIp.c_str(), &destAddr.sin_addr);

	//Gửi gói tin "probe" nhỏ để server học địa chỉ IP:port của Client
	return sendto(s, "R", 1, 0, (sockaddr*)&destAddr, sizeof(destAddr)) != SOCKET_ERROR;
}

void DataChannel::stop() {
	SOCKET s = udpSocket.exchange(INVALID_SOCKET); // atomic swap: chỉ 1 thread thực sự đóng
	if (s != INVALID_SOCKET) closesocket(s);
}