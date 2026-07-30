#include "ControlChannel.h"
#include "Session.h"
#include "CmdHandler.h"

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

    //Listen từ Client
    if (listen(this->tcpSocket, SOMAXCONN) == SOCKET_ERROR) {
        cerr << format("421 Service not available, listen failed (WSA error: {})", WSAGetLastError()) << endl;
        closesocket(this->tcpSocket);
        this->tcpSocket = INVALID_SOCKET;
        return false;
    }

    cout << "Server listening on port " << this->tcpPort << endl;
    return true;
}

// Nội dung này TRƯỚC ĐÂY nằm trong run() - xử lý 1 client, từ lúc accept tới khi QUIT/disconnect.
// Giờ mỗi client mới được accept() sẽ chạy hàm này trong 1 std::thread riêng.
void ControlChannel::handleClient(SOCKET clientSocket, string clientIp) {
    {
        lock_guard<mutex> lock(g_coutMutex);
        cout << "Client connected from " << clientIp << endl;
    }

    string greeting = "220 Service ready\r\n";
    send(clientSocket, greeting.c_str(), (int)greeting.size(), 0);

    // QUAN TRỌNG: khai báo session TRƯỚC handler, để khi hàm này kết thúc (hết scope),
    // handler bị hủy TRƯỚC (destructor của CommandHandler join() transferThread nếu còn),
    // đảm bảo không có thread phụ nào còn tham chiếu tới session sau khi session đã bị hủy.
    Session session;
    CommandHandler handler;
    handler.setControlSocket(clientSocket);
    handler.setClientIp(clientIp);
    char buffer[1024] = { 0 };

    while (true) {
        ZeroMemory(buffer, sizeof(buffer));

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

        string raw(buffer), command, argument;
        parseCmd(raw, command, argument);
        {
            lock_guard<mutex> lock(g_coutMutex);
            cout << format("[{}] Command: {} | Argument: {}", clientIp, command, argument) << endl;
        }

        string reply = handler.handle(session, command, argument);
        // reply rỗng nghĩa là STOR/RETR/APPE/STOU đã tự gửi 150 + (226/426) qua sendIntermediate
        // từ thread phụ của riêng lệnh đó (xem CmdHandler.cpp) - không cần gửi gì thêm ở đây.
        if (!reply.empty()) send(clientSocket, reply.c_str(), (int)reply.size(), 0);

        if (command == "QUIT") break;
    }

    closesocket(clientSocket);
    // Hàm return ở đây: handler bị hủy trước (join transferThread nếu còn) rồi mới tới session.
}

void ControlChannel::run() {
    // Vòng lặp accept() VÔ HẠN: mỗi client mới kết nối -> 1 thread riêng, không chặn client khác.
    while (true) {
        sockaddr_in clientAddr;
        int clientAddrLen = sizeof(clientAddr);

        SOCKET clientSocket = accept(this->tcpSocket, (sockaddr*)&clientAddr, &clientAddrLen);
        if (clientSocket == INVALID_SOCKET) {
            // tcpSocket đã bị stop() đóng (server đang tắt) -> thoát vòng lặp
            if (this->tcpSocket == INVALID_SOCKET) break;
            cerr << format("421 Service not available, accept failed (WSA error: {})", WSAGetLastError()) << endl;
            continue; // 1 client accept lỗi không nên làm sập cả server
        }

        char clientIpStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, clientIpStr, INET_ADDRSTRLEN);

        // detach: mỗi thread client tự lo vòng đời của mình (Session/CommandHandler cục bộ)
        thread(&ControlChannel::handleClient, this, clientSocket, string(clientIpStr)).detach();
    }
}

void ControlChannel::stop() {
    if (this->tcpSocket != INVALID_SOCKET) {
        closesocket(this->tcpSocket);
        this->tcpSocket = INVALID_SOCKET;
    }
}