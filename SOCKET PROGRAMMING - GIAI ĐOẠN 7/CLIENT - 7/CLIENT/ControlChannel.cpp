#include "ControlChannel.h"
#include "DataChannel.h"

ControlChannel::ControlChannel(unsigned short port, string IP) {
    this->serverTcpPort = port;
    this->serverIp = IP;
    this->tcpSocket = INVALID_SOCKET;
}

ControlChannel::~ControlChannel() {
    keepRunning = false;
    if (receiverThread.joinable()) receiverThread.join();
}

bool ControlChannel::start() {
    this->tcpSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (this->tcpSocket == INVALID_SOCKET) {
        cerr << format("421 Service not available, cannot create socket (WSA error: {})", WSAGetLastError()) << endl;
        return false;
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(this->serverTcpPort);

    if (inet_pton(AF_INET, (this->serverIp).c_str(), &serverAddr.sin_addr) <= 0) {
        cerr << "501 Syntax error in parameters, invalid IP address" << endl;
        closesocket(this->tcpSocket);
        this->tcpSocket = INVALID_SOCKET;
        return false;
    }

    if (connect(this->tcpSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        cerr << format("421 Service not available, cannot connect to server (WSA error: {})", WSAGetLastError()) << endl;
        closesocket(this->tcpSocket);
        this->tcpSocket = INVALID_SOCKET;
        return false;
    }

    cout << "200 Connected successfully, ready for commands" << endl;
    return true;
}

bool ControlChannel::parsePortArgLocal(const string& arg, unsigned short& outPort) {
    vector<int> nums;
    stringstream ss(arg);
    string token;
    while (getline(ss, token, ',')) {
        try { nums.push_back(std::stoi(token)); }
        catch (...) { return false; }
    }
    if (nums.size() != 6) return false;
    outPort = (unsigned short)(nums[4] * 256 + nums[5]);
    return true;
}

bool ControlChannel::parsePasvReply(const string& reply, unsigned short& outPort) {
    // reply dạng: "227 Entering Passive Mode (h1,h2,h3,h4,p1,p2)\r\n"
    size_t open = reply.find('(');
    size_t close = reply.find(')');
    if (open == string::npos || close == string::npos || close <= open) return false;

    string inside = reply.substr(open + 1, close - open - 1);
    vector<int> nums;
    stringstream ss(inside);
    string token;
    while (getline(ss, token, ',')) {
        try { nums.push_back(std::stoi(token)); }
        catch (...) { return false; }
    }
    if (nums.size() != 6) return false;
    outPort = (unsigned short)(nums[4] * 256 + nums[5]);
    return true;
}

bool ControlChannel::parseEmbeddedPort(const string& reply, unsigned short& outPort) {
    // Server nhúng cổng ngẫu nhiên (Active/None) dạng " PORT=<n>" vào reply "150"
    // (xem CommandHandler::appendPortIfNeeded phía Server) - đây là cách thay thế
    // cho việc dùng SERVER_DATA_PORT cố định cũ.
    const string marker = "PORT=";
    size_t pos = reply.find(marker);
    if (pos == string::npos) return false;
    pos += marker.size();
    size_t end = pos;
    while (end < reply.size() && isdigit((unsigned char)reply[end])) end++;
    if (end == pos) return false;
    try { outPort = (unsigned short)std::stoi(reply.substr(pos, end - pos)); }
    catch (...) { return false; }
    return true;
}

bool ControlChannel::autoNegotiateActivePort() {
    // Tự động làm những gì user sẽ phải gõ tay bằng lệnh "PORT h1,h2,h3,h4,p1,p2", nhưng dùng
    // 1 cổng NGẪU NHIÊN do OS cấp thay vì một số cố định (CLIENT_DATA_PORT cũ), để tránh xung đột
    // cổng khi chạy nhiều Client cùng lúc trên cùng 1 máy và không phải "đoán" cổng nào đang rảnh.

    // Bước 1: bind thử 1 UDP socket tạm với port=0 để hỏi OS "còn cổng nào trống không", rồi đóng lại.
    // DataChannel::start() (dùng thật cho RETR) sẽ tự bind lại ĐÚNG số cổng này sau đó.
    SOCKET tmp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (tmp == INVALID_SOCKET) return false;

    sockaddr_in tmpAddr{};
    tmpAddr.sin_family = AF_INET;
    tmpAddr.sin_addr.s_addr = INADDR_ANY;
    tmpAddr.sin_port = 0; // 0 = để OS tự chọn cổng còn trống
    if (bind(tmp, (sockaddr*)&tmpAddr, sizeof(tmpAddr)) == SOCKET_ERROR) {
        closesocket(tmp);
        return false;
    }

    sockaddr_in boundAddr{};
    int boundLen = sizeof(boundAddr);
    if (getsockname(tmp, (sockaddr*)&boundAddr, &boundLen) == SOCKET_ERROR) {
        closesocket(tmp);
        return false;
    }
    unsigned short ephemeralPort = ntohs(boundAddr.sin_port);
    closesocket(tmp);

    // Bước 2: lấy IP cục bộ theo góc nhìn của kết nối control (giống cách Server tự học IP cho PASV)
    sockaddr_in localAddr{};
    int localLen = sizeof(localAddr);
    if (getsockname(tcpSocket, (sockaddr*)&localAddr, &localLen) == SOCKET_ERROR) return false;

    char ipStr[INET_ADDRSTRLEN];
    if (!inet_ntop(AF_INET, &localAddr.sin_addr, ipStr, INET_ADDRSTRLEN)) return false;

    vector<int> ipParts;
    stringstream ipss(ipStr);
    string seg;
    while (getline(ipss, seg, '.')) {
        try { ipParts.push_back(std::stoi(seg)); }
        catch (...) { return false; }
    }
    if (ipParts.size() != 4) return false;

    string portCmd = format("PORT {},{},{},{},{},{}",
        ipParts[0], ipParts[1], ipParts[2], ipParts[3],
        ephemeralPort / 256, ephemeralPort % 256);

    // Cập nhật trạng thái cục bộ NGAY (không cần đợi reply "200" từ server, vì thứ tự gửi TCP
    // đảm bảo lệnh RETR gửi ngay sau đó sẽ tới server SAU lệnh PORT tự động này).
    myActivePort = ephemeralPort;
    dataMode = ClientDataMode::ACTIVE;

    send(tcpSocket, portCmd.c_str(), (int)portCmd.size(), 0);
    return true;
}

// Thực hiện data-transfer THẬT - gọi từ receiverLoop() (thread nền), BLOCKING ở đây
// KHÔNG ảnh hưởng thread đọc bàn phím -> user vẫn gõ ABOR được trong lúc hàm này đang chạy.
void ControlChannel::doDataTransfer(const string& cmdWord, const string& filename) {
    if (cmdWord == "STOR" || cmdWord == "STOU" || cmdWord == "APPE") {
        // PASSIVE: dùng cổng server đã báo qua 227. ACTIVE/NONE: dùng cổng ngẫu nhiên server vừa
        // báo qua "150" (serverUploadPort) - thay cho SERVER_DATA_PORT cố định cũ.
        unsigned short destPort = (dataMode.load() == ClientDataMode::PASSIVE) ? serverPasvPort.load() : serverUploadPort.load();
        DataChannel dc(0); // port cục bộ ephemeral - không quan trọng khi gửi đi
        if (dc.start()) {
            dc.sendFile(filename, serverIp, destPort);
            dc.stop();
        }
    }
    else if (cmdWord == "RETR") {
        ClientDataMode mode = dataMode.load();
        if (mode == ClientDataMode::PASSIVE) {
            // PASSIVE: server đang chờ ở serverPasvPort nhưng chưa biết địa chỉ client
            // -> gửi 1 gói "probe" để server học địa chỉ, rồi mới nhận file.
            DataChannel dc(0);
            if (dc.start()) {
                dc.sendProbe(serverIp, serverPasvPort.load());
                dc.receiveFile(filename);
                dc.stop();
            }
        }
        else if (mode == ClientDataMode::ACTIVE) {
            // ACTIVE (kể cả tự động qua autoNegotiateActivePort()): server sẽ tự gửi tới
            // đúng port client đã báo qua PORT (số ngẫu nhiên do OS cấp, không còn cố định).
            DataChannel dc(myActivePort.load());
            if (dc.start()) {
                dc.receiveFile(filename);
                dc.stop();
            }
        }
        else {
            // Trường hợp này KHÔNG còn xảy ra bình thường: run() luôn tự động gọi
            // autoNegotiateActivePort() trước khi gửi RETR nếu chưa có PORT/PASV nào.
            // Giữ lại làm lớp bảo vệ để không tự đoán bừa 1 cổng cố định (CLIENT_DATA_PORT cũ).
            cerr << "425 Can't open data connection: no PORT/PASV negotiated for this RETR" << endl;
        }
    }
}

// Vòng lặp chạy trong THREAD NỀN: liên tục nhận MỌI phản hồi từ server (220, 150, 226, 426, 227, 225...)
// và tự xử lý, tách biệt hoàn toàn khỏi thread đọc bàn phím.
void ControlChannel::receiverLoop() {
    char buffer[1024];
    while (keepRunning) {
        ZeroMemory(buffer, sizeof(buffer));
        int byteRecv = recv(tcpSocket, buffer, sizeof(buffer) - 1, 0);

        if (byteRecv <= 0) {
            if (keepRunning) {
                if (byteRecv == 0) cout << "\n221 Connection closed by remote host" << endl;
                else cerr << format("\n426 Connection closed, transfer aborted (WSA error: {})", WSAGetLastError()) << endl;
            }
            keepRunning = false;
            break;
        }

        string reply(buffer);
        cout << "\nServer: " << reply << endl << "ftp> " << std::flush;

        if (reply.substr(0, 3) == "227") {
            unsigned short p;
            if (parsePasvReply(reply, p)) {
                serverPasvPort = p;
                dataMode = ClientDataMode::PASSIVE;
            }
        }

        if (reply.substr(0, 3) == "150") {
            // Nếu Server không dùng Passive, nó nhúng cổng ngẫu nhiên thật (thay SERVER_DATA_PORT cũ)
            // vào reply này dưới dạng " PORT=<n>" - đọc ra để STOR/APPE/STOU biết gửi data đi đâu.
            unsigned short p;
            if (parseEmbeddedPort(reply, p)) serverUploadPort = p;

            string cmdWord, filename;
            {
                std::lock_guard<std::mutex> lock(pendingMutex);
                cmdWord = pendingCmdWord;
                filename = pendingArg;
            }
            doDataTransfer(cmdWord, filename); // BLOCKING ở đây, nhưng chỉ block thread NÀY
        }
    }
}

void ControlChannel::run() {
    char buffer[1024] = { 0 };
    int byteRecv = recv(this->tcpSocket, buffer, sizeof(buffer) - 1, 0);

    if (byteRecv > 0) cout << "Server: " << buffer << endl;
    else {
        cerr << "421 Service not available, did not receive greeting from server" << endl;
        closesocket(this->tcpSocket);
        this->tcpSocket = INVALID_SOCKET;
        WSACleanup();
        return;
    }

    // Bắt đầu thread nền nhận phản hồi + tự chạy transfer -> thread chính (bàn phím) không bao giờ
    // bị block, có thể gõ ABOR bất cứ lúc nào kể cả khi 1 transfer đang chạy.
    receiverThread = std::thread(&ControlChannel::receiverLoop, this);

    string input;
    while (keepRunning) {
        cout << "ftp> ";
        if (!std::getline(cin, input)) break;
        if (input.empty()) continue;

        // LỖI CŨ: "cmdWord = input.substr(0,4)" + "cmdArg = input.substr(5)" giả định MỌI lệnh
        // đều dài đúng 4 ký tự rồi tới 1 dấu cách (vị trí 4) nên đối số bắt đầu ở vị trí 5.
        // Điều đó đúng với lệnh 4 chữ (USER, STOR, RETR, ...) nhưng SAI với 4 lệnh 3 chữ
        // (PWD, CWD, MKD, RMD): với "MKD test", vị trí 5 là ký tự 'e' của "test" (vị trí 4 mới
        // là 't'), nên cmdArg bị cắt mất ký tự đầu -> server nhận "MKD est", tạo nhầm thư mục "est".
        // Sửa: tách theo dấu cách ĐẦU TIÊN, không giả định độ dài lệnh.
        size_t sp = input.find(' ');
        string cmdWord = (sp == string::npos) ? input : input.substr(0, sp);
        for (auto& c : cmdWord) c = toupper(c);
        string cmdArg = (sp == string::npos) ? "" : input.substr(sp + 1);

        // Nếu user gõ PORT: nhớ lại port MÌNH sẽ tự bind (để RETR sau đó dùng đúng port này)
        if (cmdWord == "PORT") {
            unsigned short p;
            if (parsePortArgLocal(cmdArg, p)) {
                myActivePort = p;
                dataMode = ClientDataMode::ACTIVE;
            }
        }

        // RETR mà chưa từng dùng PORT/PASV lần nào (dataMode == NONE): tự động "PORT" ngầm với
        // 1 cổng NGẪU NHIÊN do OS cấp trước khi gửi RETR thật - thay cho việc dùng CLIENT_DATA_PORT
        // cố định cũ (rủi ro đụng cổng khi chạy nhiều Client, hoặc cổng bị chiếm bởi tiến trình khác).
        if (cmdWord == "RETR" && dataMode.load() == ClientDataMode::NONE) {
            autoNegotiateActivePort();
        }

        // Ghi nhớ lệnh vừa gửi TRƯỚC khi send(), để thread nền biết cần làm gì khi thấy "150"
        {
            std::lock_guard<std::mutex> lock(pendingMutex);
            pendingCmdWord = cmdWord;
            pendingArg = cmdArg;
        }

        send(tcpSocket, input.c_str(), (int)input.size(), 0);

        if (cmdWord == "QUIT") { keepRunning = false; break; }
    }

    if (receiverThread.joinable()) receiverThread.join();
}

void ControlChannel::stop() {
    keepRunning = false;
    if (this->tcpSocket != INVALID_SOCKET) {
        closesocket(this->tcpSocket); // làm recv() đang block ở receiverThread thoát ra ngay
        this->tcpSocket = INVALID_SOCKET;
    }
}