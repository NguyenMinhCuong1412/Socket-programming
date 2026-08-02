#include "ControlChannel.h"
#include "DataChannel.h"
#include "HashUtil.h"

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

// Thực hiện data-transfer THẬT - gọi từ receiverLoop() (thread nền), BLOCKING ở đây
// KHÔNG ảnh hưởng thread đọc bàn phím -> user vẫn gõ ABOR được trong lúc hàm này đang chạy.
void ControlChannel::doDataTransfer(const string& cmdWord, const string& filename) {
    if (cmdWord == "STOR" || cmdWord == "STOU" || cmdWord == "APPE") {
        unsigned short destPort = (dataMode.load() == ClientDataMode::PASSIVE) ? serverPasvPort.load() : SERVER_DATA_PORT;
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
            // ACTIVE: server sẽ tự gửi tới đúng port client đã báo qua PORT
            DataChannel dc(myActivePort.load());
            if (dc.start()) {
                dc.receiveFile(filename);
                dc.stop();
            }
        }
        else {
            // Mặc định (chưa dùng PORT/PASV lần nào): giữ hành vi cũ như GĐ4
            DataChannel dc(CLIENT_DATA_PORT);
            if (dc.start()) {
                dc.receiveFile(filename);
                dc.stop();
            }
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

        string cmdWord5 = input.substr(0, 5);
        for (auto& c : cmdWord5) c = toupper(c);
        if (cmdWord5 == "LHASH") {
            string filename = input.size() > 6 ? input.substr(6) : "";
            if (filename.empty()) {
                cout << "501 Syntax error in parameters, filename required for LHASH\n";
            } else {
                string h = computeFileSHA256(filename);
                if (h.empty()) {
                    cout << format("550 Local file unavailable or hash error for '{}'\n", filename);
                } else {
                    cout << format("Local SHA-256 ({}): {}\n", filename, h);
                }
            }
            continue; // Không gửi LHASH lên server
        }

        string cmdWord = input.substr(0, 4);
        for (auto& c : cmdWord) c = toupper(c);
        string cmdArg = input.size() > 5 ? input.substr(5) : "";

        // Nếu user gõ PORT: nhớ lại port MÌNH sẽ tự bind (để RETR sau đó dùng đúng port này)
        if (cmdWord == "PORT") {
            unsigned short p;
            if (parsePortArgLocal(cmdArg, p)) {
                myActivePort = p;
                dataMode = ClientDataMode::ACTIVE;
            }
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