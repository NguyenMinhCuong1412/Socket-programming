#include "ControlChannel.h"

ControlChannel::ControlChannel(unsigned short port, string IP) {
	this->port = port;
	this->serverIp = IP;
	this->clientSocket = INVALID_SOCKET;
}

bool ControlChannel::start() {
    //Tạo socket
    this->clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (this->clientSocket == INVALID_SOCKET) {
        cerr << format("421 Service not available, cannot create socket (WSA error: {})", WSAGetLastError()) << endl;
        return false;
    }

    //Định danh địa chỉ Server cần kết nối tới
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(this->port);

    if (inet_pton(AF_INET, (this->serverIp).c_str(), &serverAddr.sin_addr) <= 0) {
        cerr << "501 Syntax error in parameters, invalid IP address" << endl;
        closesocket(this->clientSocket);
        this->clientSocket = INVALID_SOCKET;
        return false;
    }

    //Kết nối đến Server
    if (connect(this->clientSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        cerr << format("421 Service not available, cannot connect to server (WSA error: {})", WSAGetLastError()) << endl;
        closesocket(clientSocket);
        this->clientSocket = INVALID_SOCKET;
        return false;
    }

    return true;
}

void ControlChannel::run() {
    //Nhận dữ liệu từ Server 
    char buffer[1024] = { 0 };
    int byteRecv = recv(this->clientSocket, buffer, sizeof(buffer) - 1, 0);

    if (byteRecv > 0) cout << "Server: " << buffer << endl;
    else if (byteRecv == 0) cout << "221 Connection closed by remote host" << endl;
    else cerr << format("426 Connection closed, transfer aborted (WSA error: {})", WSAGetLastError()) << endl;
}

void ControlChannel::stop() {
    if (this->clientSocket != INVALID_SOCKET) {
        closesocket(this->clientSocket);
        this->clientSocket = INVALID_SOCKET;
    }
}
