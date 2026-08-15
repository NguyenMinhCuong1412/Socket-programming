// ======================================================================
// ControlChannel.cpp — CÀI ĐẶT KÊNH ĐIỀU KHIỂN (TCP) CỦA SERVER
//    Quản lý kết nối TCP đa luồng:
//    - start(): tạo socket TCP, bind, listen
//    - run(): vòng lặp accept() → tạo thread mới cho mỗi Client
//    - handleClient(): xử lý lệnh FTP từ Client trong thread riêng
//    - adminConsoleLoop(): thread admin console cho quản trị viên Server
// ======================================================================
#include "ControlChannel.h"
#include "Session.h"
#include "CmdHandler.h"
#include "SessionRegistry.h"

// Xử lý một Client kết nối — chạy trong thread riêng
// Mỗi Client có Session riêng (lưu trạng thái) và CommandHandler riêng (xử lý lệnh)
void ControlChannel::handleClient(SOCKET clientSocket, string clientIp) {
    {
        lock_guard<mutex> lock(g_coutMutex);  // Bảo vệ console output — tránh xen lẫn giữa các thread
        cout << "Client connected from " << clientIp << endl;
    }

    // Đăng ký Client vào bảng phiên và in bảng trạng thái
    SessionRegistry::add(clientSocket, clientIp);
    SessionRegistry::printTable();

    // Gửi lời chào FTP 220 (theo chuẩn FTP RFC 959)
    string greeting = "220 Service ready\r\n";
    send(clientSocket, greeting.c_str(), (int)greeting.size(), 0);

    Session session;           // Trạng thái phiên riêng cho Client này
    CommandHandler handler;    // Bộ xử lý lệnh FTP
    handler.setControlSocket(clientSocket);  // Đặt socket TCP để gửi phản hồi
    handler.setClientIp(clientIp);           // Đặt IP Client (dùng cho Passive mode)
    char buffer[1024] = { 0 };
    string streamBuffer = "";  // Buffer tích lũy dữ liệu TCP
    bool shouldQuit = false;

    // Vòng lặp nhận và xử lý lệnh FTP từ Client
    while (!shouldQuit) {
        ZeroMemory(buffer, sizeof(buffer));

        // Nhận dữ liệu TCP từ Client
        int byteRecv = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
        if (byteRecv <= 0) {
            lock_guard<mutex> lock(g_coutMutex);
            if (byteRecv == 0) cout << format("Client {} disconnected", clientIp) << endl;
            else cerr << format("426 Connection closed, transfer aborted (WSA error: {})", WSAGetLastError()) << endl;
            break;
        }

        // Thêm dữ liệu nhận vào buffer tích lũy (xử lý trường hợp lệnh đến rời rạc)
        streamBuffer.append(buffer, byteRecv);

        // Tách từng dòng lệnh (kết thúc bằng '\n' theo chuẩn FTP)
        size_t pos = 0;
        while ((pos = streamBuffer.find('\n')) != string::npos) {
            string line = streamBuffer.substr(0, pos);
            streamBuffer.erase(0, pos + 1);

            // Phân tích dòng lệnh thành tên lệnh (command) và tham số (argument)
            string command, argument;
            parseCmd(line, command, argument);
            if (command.empty()) continue;

            // Log lệnh nhận được ra console Server
            {
                lock_guard<mutex> lock(g_coutMutex);
                cout << format("[{}] Command: {} | Argument: {}", clientIp, command, argument) << endl;
            }

            // Gọi CommandHandler để xử lý lệnh → trả về phản hồi FTP
            string reply = handler.handle(session, command, argument);
            if (!reply.empty()) send(clientSocket, reply.c_str(), (int)reply.size(), 0);

            // Cập nhật bảng phiên với trạng thái mới nhất
            SessionRegistry::update(clientSocket, session.getUserName(), session.getLoggedIn(), session.getDir(), command);

            // Lệnh QUIT → kết thúc vòng lặp
            if (command == "QUIT") {
                shouldQuit = true;
                break;
            }
        }
    }

    // Xóa Client khỏi bảng phiên và in bảng cập nhật
    SessionRegistry::remove(clientSocket);
    SessionRegistry::printTable();

    closesocket(clientSocket);
}

// Admin console: đọc lệnh từ stdin Server
// Hỗ trợ lệnh: "sessions"/"who" (hiển thị bảng phiên), "help" (trợ giúp)
void ControlChannel::adminConsoleLoop() {
    string line;
    while (true) {
        if (!getline(cin, line)) break;  // EOF → thoát
        string cmd = line;
        for (auto& c : cmd) c = tolower((unsigned char)c);  // Chuyển sang chữ thường

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

// Constructor
ControlChannel::ControlChannel(unsigned short port) {
	this->tcpPort = port;
    this->tcpSocket = INVALID_SOCKET;
}

// Tạo socket TCP, bind, listen
bool ControlChannel::start() {
    // Tạo socket TCP
    this->tcpSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (this->tcpSocket == INVALID_SOCKET) {
        cerr << format("421 Service not available, cannot create socket (WSA error: {})", WSAGetLastError()) << endl;
        return false;
    }

    // Cấu hình địa chỉ bind: lắng nghe mọi interface, cổng tcpPort
    sockaddr_in serverAddrTcp = {};
    serverAddrTcp.sin_family = AF_INET;
    serverAddrTcp.sin_addr.s_addr = INADDR_ANY;
    serverAddrTcp.sin_port = htons(this->tcpPort);

    // Bind socket vào địa chỉ
    if (bind(this->tcpSocket, (sockaddr*)&serverAddrTcp, sizeof(serverAddrTcp)) == SOCKET_ERROR) {
        cerr << format("421 Service not available, bind failed (WSA error: {})", WSAGetLastError()) << endl;
        closesocket(this->tcpSocket);
        this->tcpSocket = INVALID_SOCKET;
        return false;
    }

    // Bắt đầu lắng nghe — SOMAXCONN: hàng đợi kết nối tối đa do hệ thống quyết định
    if (listen(this->tcpSocket, SOMAXCONN) == SOCKET_ERROR) {
        cerr << format("421 Service not available, listen failed (WSA error: {})", WSAGetLastError()) << endl;
        closesocket(this->tcpSocket);
        this->tcpSocket = INVALID_SOCKET;
        return false;
    }

    cout << "Server listening on port " << this->tcpPort << endl;
    return true;
}

// Vòng lặp accept() — mỗi Client kết nối → tạo thread riêng để xử lý
void ControlChannel::run() {
    // Khởi chạy admin console trên thread riêng
    thread(&ControlChannel::adminConsoleLoop, this).detach();
    cout << "Type 'sessions' anytime to view the active session table (type 'help' for more)." << endl;

    // Vòng lặp accept() — chờ kết nối mới từ Client
    while (true) {
        sockaddr_in clientAddr = {};
        int clientAddrLen = sizeof(clientAddr);

        // accept(): block cho đến khi có Client kết nối
        SOCKET clientSocket = accept(this->tcpSocket, (sockaddr*)&clientAddr, &clientAddrLen);
        if (clientSocket == INVALID_SOCKET) {
            if (this->tcpSocket == INVALID_SOCKET) break;  // Server đã dừng
            cerr << format("421 Service not available, accept failed (WSA error: {})", WSAGetLastError()) << endl;
            continue;
        }

        // Chuyển địa chỉ IP Client từ nhị phân → chuỗi
        char clientIpStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, clientIpStr, INET_ADDRSTRLEN);

        // Tạo thread riêng để xử lý Client — detach: thread tự giải phóng khi kết thúc
        thread(&ControlChannel::handleClient, this, clientSocket, string(clientIpStr)).detach();
    }
}

// Đóng listen socket → các accept() đang chờ sẽ trả về lỗi → thoát vòng lặp
void ControlChannel::stop() {
    if (this->tcpSocket != INVALID_SOCKET) {
        closesocket(this->tcpSocket);
        this->tcpSocket = INVALID_SOCKET;
    }
}