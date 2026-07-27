#include "ControlChannel.h"

ControlChannel::ControlChannel(unsigned short port) {
	this->port = port;
	this->serverSocket = INVALID_SOCKET;
}

bool ControlChannel::start() {
    //Tạo socket
    this->serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (this->serverSocket == INVALID_SOCKET) {
        cerr << format("421 Service not available, cannot create socket (WSA error: {})", WSAGetLastError()) << endl;
        return false;
    }

    //Định danh địa chỉ Server
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(this->port);

    //Bind socket
    if (bind(this->serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        cerr << format("421 Service not available, bind failed (WSA error: {})", WSAGetLastError()) << endl;
        closesocket(this->serverSocket);
        this->serverSocket = INVALID_SOCKET;
        return false;
    }

    //Listen từ Client
    if (listen(this->serverSocket, SOMAXCONN) == SOCKET_ERROR) {
        cerr << format("421 Service not available, listen failed (WSA error: {})", WSAGetLastError()) << endl;
        closesocket(this->serverSocket);
        this->serverSocket = INVALID_SOCKET;
        return false;
    }

    cout << "Server listening on port " << this->port << endl;
    return true;
}

void ControlChannel::run() {
    SOCKET clientSocket = accept(serverSocket, NULL, NULL);
    if (clientSocket == INVALID_SOCKET) cerr << format("421 Service not available, accept failed (WSA error: {})", WSAGetLastError()) << endl;
    else {
        cout << "220 Connection accepted, service ready for new user" << endl;

        const char* msg = "Xin chào Client";
        send(clientSocket, msg, (int)strlen(msg), 0);

        closesocket(clientSocket);
    }
}

void ControlChannel::stop() {
    if (this->serverSocket != INVALID_SOCKET) {
        closesocket(this->serverSocket);
        this->serverSocket = INVALID_SOCKET;
    }
}