#include "ControlChannel.h"
#include "Session.h"
#include "CmdHandler.h"

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
    //Accept Client
    SOCKET clientSocket = accept(this->serverSocket, NULL, NULL);
    if (clientSocket == INVALID_SOCKET) {
        cerr << format("421 Service not available, accept failed (WSA error: {})", WSAGetLastError()) << endl;
        closesocket(this->serverSocket);
        this->serverSocket = INVALID_SOCKET;
        WSACleanup();
        return;
    }
    cout << "Client connected" << endl;

    string greeting = "220 Service ready\r\n";
    send(clientSocket, greeting.c_str(), (int)greeting.size(), 0);

    //Vòng lặp gửi phản hồi / nhận lệnh
    Session session;
    CommandHandler handler;
    char buffer[1024] = { 0 };

    while (true) {
        ZeroMemory(buffer, sizeof(buffer));
        int byteRecv = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);

        if (byteRecv == 0) {
            cout << "Client disconnected." << endl;
            break;
        }
        else if (byteRecv < 0) {
            cerr << format("426 Connection closed, transfer aborted (WSA error: {})", WSAGetLastError()) << endl;
            break;
        }

        string raw(buffer), command, argument;
        parseCmd(raw, command, argument);

        cout << format("Command: {} | Argument: {}", command, argument) << endl;

        string reply = handler.handle(session, command, argument);
        send(clientSocket, reply.c_str(), (int)reply.size(), 0);

        if (command == "QUIT") break;
    }

    closesocket(clientSocket);
}

void ControlChannel::stop() {
    if (this->serverSocket != INVALID_SOCKET) {
        closesocket(this->serverSocket);
        this->serverSocket = INVALID_SOCKET;
    }
}