#include "ControlChannel.h"
#include "Session.h"
#include "CmdHandler.h"

void ControlChannel::handleClient(SOCKET clientSocket, string clientIp) {
    //Tạo vòng đời cục bộ - bảo vệ các hàm bên trong tránh các luồng khác đụng vào
    { //Mở phạm vi - vòng đời bắt đầu
        lock_guard<mutex> lock(g_coutMutex); 
        cout << "Client connected from " << clientIp << endl;
    } //Đóng phạm vi - vòng đời kết thúc

    //Gửi mã chào FTP
    string greeting = "220 Service ready\r\n";
    send(clientSocket, greeting.c_str(), (int)greeting.size(), 0);

    //Khởi tạo phiên làm việc và công cụ xử lý lệnh riêng cho Client
    Session session;
    CommandHandler handler;
    handler.setControlSocket(clientSocket);
    handler.setClientIp(clientIp);
    char buffer[1024] = { 0 };  //Khởi tạo vùng nhớ cố định 

    while (true) {
        //Làm sạch vùng nhớ nhận lệnh
        ZeroMemory(buffer, sizeof(buffer));

        //Đọc dữ liệu từ Socket do Client gửi và kiểm tra
        int byteRecv = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
        if (byteRecv == 0) {
            lock_guard<mutex> lock(g_coutMutex);
            cout << format("Client {} disconnected", clientIp) << endl;
            break;
        }
        else if (byteRecv < 0) {
            lock_guard<mutex> lock(g_coutMutex);
            cerr << format("426 Connection closed, transfer aborted (WSA error: {})", WSAGetLastError()) << endl;
            break;
        }

        //Phân tích lệnh
        string raw(buffer), command, argument;
        parseCmd(raw, command, argument);

        //Tạo vòng đời cục bộ - bảo vệ các hàm bên trong tránh các luồng khác đụng vào
        { //Mở phạm vi - vòng đời bắt đầu
            lock_guard<mutex> lock(g_coutMutex);
            cout << format("[{}] Command: {} | Argument: {}", clientIp, command, argument) << endl;
        } //Đóng phạm vi - vòng đời kết thúc

        //Thực thi lệnh và phản hồi
        string reply = handler.handle(session, command, argument);
        if (!reply.empty()) send(clientSocket, reply.c_str(), (int)reply.size(), 0);

        if (command == "QUIT") break;
    }

    //Đóng riêng Client muốn thoát
    closesocket(clientSocket);
}

ControlChannel::ControlChannel(unsigned short port) {
    this->tcpPort = port;
    this->tcpSocket = INVALID_SOCKET;
}

bool ControlChannel::start() {
    //Tạo TCP-socket 
    this->tcpSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (this->tcpSocket == INVALID_SOCKET) {
        cerr << format("421 Service not available, cannot create socket (WSA error: {})", WSAGetLastError()) << endl;
        return false;
    }

    //Định danh địa chỉ Server-TCP
    sockaddr_in serverAddrTcp;
    serverAddrTcp.sin_family = AF_INET;
    serverAddrTcp.sin_addr.s_addr = INADDR_ANY;
    serverAddrTcp.sin_port = htons(this->tcpPort);

    //Bind TCP-socket với địa chỉ Server-TCP
    if (bind(this->tcpSocket, (sockaddr*)&serverAddrTcp, sizeof(serverAddrTcp)) == SOCKET_ERROR) {
        cerr << format("421 Service not available, bind failed (WSA error: {})", WSAGetLastError()) << endl;
        closesocket(this->tcpSocket);
        this->tcpSocket = INVALID_SOCKET;
        return false;
    }

    //Listen từ Client-TCP
    if (listen(this->tcpSocket, SOMAXCONN) == SOCKET_ERROR) {
        cerr << format("421 Service not available, listen failed (WSA error: {})", WSAGetLastError()) << endl;
        closesocket(this->tcpSocket);
        this->tcpSocket = INVALID_SOCKET;
        return false;
    }

    cout << "Server listening on port " << this->tcpPort << endl;
    return true;
}

void ControlChannel::run() {
    while (true) {
        //Lấy thông tin Client được accept: các thông tin mạng + độ lớn vùng nhớ của Client 
        sockaddr_in clientAddr;
        int clientAddrLen = sizeof(clientAddr);

        //Accept Client-TCP
        SOCKET clientSocket = accept(this->tcpSocket, (sockaddr*)&clientAddr, &clientAddrLen);
        if (clientSocket == INVALID_SOCKET) {
            //tcpSocket đã bị stop() đóng (server đang tắt) -> thoát
            if (this->tcpSocket == INVALID_SOCKET) break;
            cerr << format("421 Service not available, accept failed (WSA error: {})", WSAGetLastError()) << endl;
            continue; //1 client accept lỗi không làm sập cả server
        }

        //Vùng nhớ chứa địa chỉ IP Client kết nối tới
        char clientIpStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, clientIpStr, INET_ADDRSTRLEN);

        //detach: mỗi thread Client tự lo vòng đời (Session/CommandHandler cục bộ)
        thread(&ControlChannel::handleClient, this, clientSocket, string(clientIpStr)).detach();
    }
}

void ControlChannel::stop() {
    if (this->tcpSocket != INVALID_SOCKET) {
        closesocket(this->tcpSocket);
        this->tcpSocket = INVALID_SOCKET;
    }
}