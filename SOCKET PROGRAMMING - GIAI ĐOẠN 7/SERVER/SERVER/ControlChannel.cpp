#include "ControlChannel.h"
#include "Session.h"
#include "CmdHandler.h"
#include "SessionRegistry.h"

void ControlChannel::handleClient(SOCKET clientSocket, string clientIp) {
    //Tạo vòng đời cục bộ - bảo vệ các hàm bên trong tránh các luồng khác đụng vào
    { //Mở phạm vi - vòng đời bắt đầu
        lock_guard<mutex> lock(g_coutMutex); 
        cout << "Client connected from " << clientIp << endl;
    } //Đóng phạm vi - vòng đời kết thúc

    //Đăng ký session này vào bảng session đang hoạt động + in bảng để giám khảo/log thấy ngay lúc connect
    SessionRegistry::add(clientSocket, clientIp);
    SessionRegistry::printTable();

    //Gửi mã chào FTP
    string greeting = "220 Service ready\r\n";
    send(clientSocket, greeting.c_str(), (int)greeting.size(), 0);

    //Khởi tạo phiên làm việc và công cụ xử lý lệnh riêng cho Client-TCP
    Session session;
    CommandHandler handler;
    handler.setControlSocket(clientSocket);
    handler.setClientIp(clientIp);
    char buffer[1024] = { 0 }; //Khởi tạo vùng nhớ cố định 

    while (true) {
        ZeroMemory(buffer, sizeof(buffer)); //Làm sạch vùng nhớ nhận lệnh

        //Đọc dữ liệu từ socket do Client-TCP gửi và kiểm tra
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

        //Cập nhật bảng session sau MỖI lệnh: user, trạng thái login, thư mục hiện tại, lệnh vừa chạy
        SessionRegistry::update(clientSocket, session.getUserName(), session.getLoggedIn(), session.getDir(), command);

        if (command == "QUIT") break;
    }

    //Gỡ session khỏi bảng + in lại bảng để log phản ánh đúng trạng thái mới nhất
    SessionRegistry::remove(clientSocket);
    SessionRegistry::printTable();

    closesocket(clientSocket); //Đóng riêng Client-TCP muốn thoát
}

void ControlChannel::adminConsoleLoop() {
    //Luồng riêng đọc lệnh admin từ bàn phím Server, KHÔNG ảnh hưởng vòng lặp accept() chính
    //Gõ "sessions" bất cứ lúc nào để xem bảng session đang hoạt động (phục vụ log/demo trực tiếp)
    string line;
    while (true) {
        if (!getline(cin, line)) break;
        string cmd = line;
        for (auto& c : cmd) c = tolower((unsigned char)c);

        if (cmd == "sessions" || cmd == "who") SessionRegistry::printTable();
        else if (cmd == "help") {
            lock_guard<mutex> lock(g_coutMutex);
            cout << "Admin commands: sessions (show active session table) | help" << endl;
        }
        else if (!cmd.empty()) {
            lock_guard<mutex> lock(g_coutMutex);
            cout << "Unknown admin command. Type 'help'." << endl;
        }
    }
}

ControlChannel::ControlChannel(unsigned short port) {
	this->tcpPort = port;            //CONTROL_PORT = 8080
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
    //Khởi động luồng console admin (gõ "sessions" để xem bảng session đang hoạt động) - detach vì sống cùng vòng đời server
    thread(&ControlChannel::adminConsoleLoop, this).detach();
    cout << "Type 'sessions' anytime to view the active session table (type 'help' for more)." << endl;

    while (true) {
        //Lấy thông tin Client-TCP được accept: các thông tin mạng + độ lớn vùng nhớ của Client-TCP 
        sockaddr_in clientAddr;
        int clientAddrLen = sizeof(clientAddr);

        //Accept Client-TCP
        SOCKET clientSocket = accept(this->tcpSocket, (sockaddr*)&clientAddr, &clientAddrLen);
        if (clientSocket == INVALID_SOCKET) {
            if (this->tcpSocket == INVALID_SOCKET) break; //tcpSocket đã bị stop() đóng (server đang tắt) -> thoát
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