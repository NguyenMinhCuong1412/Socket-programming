#include "DataChannel.h"

DataChannel::DataChannel(unsigned short port) {
	this->udpPort = port;
	this->udpSocket = INVALID_SOCKET;
}

bool DataChannel::start() {
	//Tạo UDP-socket 
	this->udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (this->udpSocket == INVALID_SOCKET) {
		cerr << format("425 Can't open data connection, cannot create socket (WSA error: {})", WSAGetLastError()) << endl;
		return false;
	}

	//Định danh địa chỉ Server-UDP
	sockaddr_in serverAddrUdp;
	serverAddrUdp.sin_family = AF_INET;            //Loại mạng
	serverAddrUdp.sin_addr.s_addr = INADDR_ANY;    //IP kết nối tới
	serverAddrUdp.sin_port = htons(this->udpPort); //Cổng kết nối = SERVER_DATA_PORT

	//Bind UDP-socket với địa chỉ Server-UDP
	if (bind(this->udpSocket, (sockaddr*)&serverAddrUdp, sizeof(serverAddrUdp)) == SOCKET_ERROR) {
		cerr << format("425 Can't open data connection, bind failed (WSA error: {})", WSAGetLastError()) << endl;
		closesocket(this->udpSocket);
		this->udpSocket = INVALID_SOCKET;
		return false;
	}

	return true;
}

bool DataChannel::receiveFile(const string& filepath, bool append) {
	//Chế độ nối cuối -> ios::app - Chế độ ghi đè -> ios::trunc
	ios::openmode mode = ios::binary | (append ? ios::app : ios::trunc);
	ofstream out(filepath, mode); //Mở chế độ ghi file
	if (!out.is_open()) {
		cerr << format("550 File unavailable, cannot open '{}' for writing", filepath) << endl;
		return false;
	}

	char buffer[CHUNK_SIZE]; //Vùng nhớ nhận dữ liệu - kích thước giới hạn

	//Dùng để lấy các thông tin của Client-UDP gửi dữ liệu tới
	sockaddr_in senderAddr;                 //Thông tin liên quan: loại mạng + IP + PORT
	int senderAddrLen = sizeof(senderAddr); //Kích thước vùng nhớ của địa chỉ Client-UDP

	while (true) {
		//Kiểm tra dữ liệu
		int byteRecv = recvfrom(this->udpSocket, buffer, CHUNK_SIZE, 0, (sockaddr*)&senderAddr, &senderAddrLen);
		if (byteRecv == 0) break;  //EOF
		if (byteRecv == SOCKET_ERROR) { //byteRecv < 0
			cerr << format("426 Connection closed, transfer aborted (WSA error: {})", WSAGetLastError()) << endl;
			out.close();
			return false;
		}
		out.write(buffer, byteRecv); //Ghi dữ liệu
	}

	out.close(); //Đóng chế độ ghi file
	return true;
}

bool DataChannel::sendFile(const string& filepath, const string& destIp, unsigned short destPort) {
	ifstream in(filepath, ios::binary); //Mở chế độ đọc file
	if (!in.is_open()) {
		cerr << format("550 File unavailable, cannot open '{}' for reading", filepath) << endl;
		return false;
	}

	//Dùng để lấy các thông tin của Client-UDP nhận dữ liệu
	sockaddr_in destAddr;
	destAddr.sin_family = AF_INET;                          //Loại mạng
	destAddr.sin_port = htons(destPort);                    //Cổng kết nối = CLIENT_DATA_PORT
	inet_pton(AF_INET, destIp.c_str(), &destAddr.sin_addr); //Chuyển đổi: định dạng chuỗi string/mảng char -> định dạng IP hệ thống

	char buffer[CHUNK_SIZE]; //Vùng nhớ gửi dữ liệu - kích thước giới hạn
	while (in.read(buffer, CHUNK_SIZE) || in.gcount() > 0) {
		int byteToSend = (int)in.gcount(); //gcount: đếm số lượng byte thực tế của lệnh đọc gần nhất (in.read) 
		sendto(this->udpSocket, buffer, byteToSend, 0, (sockaddr*)&destAddr, sizeof(destAddr));
	}
	sendto(this->udpSocket, "", 0, 0, (sockaddr*)&destAddr, sizeof(destAddr)); //EOF

	in.close(); //Đóng chế độ đọc file
	return true; 
}

void DataChannel::stop() {
	if (this->udpSocket != INVALID_SOCKET) {
		closesocket(this->udpSocket);
		this->udpSocket = INVALID_SOCKET;
	}
}