#include "ControlChannel.h"
#include "Session.h"
#include "CmdHandler.h"

ControlChannel::ControlChannel(unsigned short port) {
    this->tcpPort = port;
    this->tcpSocket = INVALID_SOCKET;
}

bool ControlChannel::start() {
    //Tạo socket TCP
    this->tcpSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (this->tcpSocket == INVALID_SOCKET) {
        cerr << format("421 Service not available, cannot create socket (WSA error: {})", WSAGetLastError()) << endl;
        return false;
    }

    //Định danh địa chỉ Server
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(this->tcpPort);

    //Bind socket
    if (bind(this->tcpSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        cerr << format("421 Service not available, bind failed (WSA error: {})", WSAGetLastError()) << endl;
        closesocket(this->tcpSocket);     //Giải phóng tài nguyên liên quan đến socket
        this->tcpSocket = INVALID_SOCKET; //Đánh dấu đã vô hiệu lực, tránh giải phóng nhầm lúc sau
        return false;
    }

    //Listen từ Client
    if (listen(this->tcpSocket, SOMAXCONN) == SOCKET_ERROR) {
        cerr << format("421 Service not available, listen failed (WSA error: {})", WSAGetLastError()) << endl;
        closesocket(this->tcpSocket);     //Giải phóng tài nguyên liên quan đến socket
        this->tcpSocket = INVALID_SOCKET; //Đánh dấu đã vô hiệu lực, tránh giải phóng nhầm lúc sau
        return false;
    }

    cout << "Server listening on port " << this->tcpPort << endl;
    return true;
}

void ControlChannel::run() {
    //Lấy các thông tin của Client kết nối tới
    sockaddr_in clientAddr;
    int clientAddrLen = sizeof(clientAddr);

    //Accept Client
    SOCKET clientSocket = accept(this->tcpSocket, (sockaddr*)&clientAddr, &clientAddrLen);
    if (clientSocket == INVALID_SOCKET) {
        cerr << format("421 Service not available, accept failed (WSA error: {})", WSAGetLastError()) << endl;
        closesocket(this->tcpSocket);
        this->tcpSocket = INVALID_SOCKET;
        WSACleanup();
        return;
    }

    //Vùng nhớ chứa địa chỉ của Client
    char clientIpStr[INET_ADDRSTRLEN];
    //Chuyển đổi từ định dạng IP hệ thống -> định dạng chuỗi string/mảng char
    inet_ntop(AF_INET, &clientAddr.sin_addr, clientIpStr, INET_ADDRSTRLEN); //INET_ADDRSTRLEN: độ dài tối đa IPv4 - 15 + 1 '\0' = 16
    cout << "Client connected from " << clientIpStr << endl;

    //Gửi lời chào xác nhận Client có thể giao tiếp
    string greeting = "220 Service ready\r\n";
    send(clientSocket, greeting.c_str(), (int)greeting.size(), 0);

    //Vòng lặp nhận lệnh/gửi phản hồi
    Session session;
    CommandHandler handler;
    handler.setControlSocket(clientSocket); //STOR/RETR gửi được "150" qua control socket
    handler.setClientIp(clientIpStr);       //RETR biết địa chỉ đích
    char buffer[1024] = { 0 };

    while (true) {
        //Làm sạch vùng nhận lệnh
        ZeroMemory(buffer, sizeof(buffer));

        //Kiểm tra lệnh
        int byteRecv = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
        if (byteRecv == 0) {
            cout << "Client disconnected." << endl;
            break;
        }
        else if (byteRecv < 0) {
            cerr << format("426 Connection closed, transfer aborted (WSA error: {})", WSAGetLastError()) << endl;
            break;
        }

        //Xử lý và in lệnh
        string raw(buffer), command, argument;
        parseCmd(raw, command, argument);
        cout << format("Command: {} | Argument: {}", command, argument) << endl;

        //Phản hồi 
        string reply = handler.handle(session, command, argument);
        send(clientSocket, reply.c_str(), (int)reply.size(), 0);

        if (command == "QUIT") break;
    }

    closesocket(clientSocket);
}

void ControlChannel::stop() {
    if (this->tcpSocket != INVALID_SOCKET) {
        closesocket(this->tcpSocket);
        this->tcpSocket = INVALID_SOCKET;
    }
}