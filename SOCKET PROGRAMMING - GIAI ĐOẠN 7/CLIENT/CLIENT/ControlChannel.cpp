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
        try { nums.push_back(stoi(token)); }
        catch (...) { return false; }
    }
    if (nums.size() != 6) return false;
    outPort = (unsigned short)(nums[4] * 256 + nums[5]);
    return true;
}

bool ControlChannel::parsePasvReply(const string& reply, unsigned short& outPort) {
    size_t open = reply.find('(');
    size_t close = reply.find(')');
    if (open == string::npos || close == string::npos || close <= open) return false;

    string inside = reply.substr(open + 1, close - open - 1);
    vector<int> nums;
    stringstream ss(inside);
    string token;
    while (getline(ss, token, ',')) {
        try { nums.push_back(stoi(token)); }
        catch (...) { return false; }
    }
    if (nums.size() != 6) return false;
    outPort = (unsigned short)(nums[4] * 256 + nums[5]);
    return true;
}

bool ControlChannel::parseEmbeddedPort(const string& reply, unsigned short& outPort) {
    const string marker = "PORT=";
    size_t pos = reply.find(marker);
    if (pos == string::npos) return false;
    pos += marker.size();
    size_t end = pos;
    while (end < reply.size() && isdigit((unsigned char)reply[end])) end++;
    if (end == pos) return false;
    try { outPort = (unsigned short)stoi(reply.substr(pos, end - pos)); }
    catch (...) { return false; }
    return true;
}

bool ControlChannel::autoNegotiateActivePort() {
    //Tự động làm những gì user sẽ phải gõ tay bằng lệnh "PORT h1,h2,h3,h4,p1,p2", dùng 1 cổng NGẪU NHIÊN do OS cấp
    SOCKET tmp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (tmp == INVALID_SOCKET) return false;

    sockaddr_in tmpAddr{};
    tmpAddr.sin_family = AF_INET;
    tmpAddr.sin_addr.s_addr = INADDR_ANY;
    tmpAddr.sin_port = 0; //0 = để OS tự chọn cổng còn trống
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

    sockaddr_in localAddr{};
    int localLen = sizeof(localAddr);
    if (getsockname(tcpSocket, (sockaddr*)&localAddr, &localLen) == SOCKET_ERROR) return false;

    char ipStr[INET_ADDRSTRLEN];
    if (!inet_ntop(AF_INET, &localAddr.sin_addr, ipStr, INET_ADDRSTRLEN)) return false;

    vector<int> ipParts;
    stringstream ipss(ipStr);
    string seg;
    while (getline(ipss, seg, '.')) {
        try { ipParts.push_back(stoi(seg)); }
        catch (...) { return false; }
    }
    if (ipParts.size() != 4) return false;

    string portCmd = format("PORT {},{},{},{},{},{}",
        ipParts[0], ipParts[1], ipParts[2], ipParts[3],
        ephemeralPort / 256, ephemeralPort % 256);

    myActivePort = ephemeralPort;
    dataMode = DataMode::ACTIVE;

    send(tcpSocket, portCmd.c_str(), (int)portCmd.size(), 0);
    return true;
}

void ControlChannel::doDataTransfer(const string& cmdWord, const string& filename) {
    if (cmdWord == "STOR" || cmdWord == "STOU" || cmdWord == "APPE") {
        unsigned short destPort = (dataMode.load() == DataMode::PASSIVE) ? serverPasvPort.load() : serverUploadPort.load();
        DataChannel dc(0); //port cục bộ ephemeral
        if (dc.start()) {
            dc.sendFile(filename, serverIp, destPort);
            dc.stop();
        }
    }
    else if (cmdWord == "RETR") {
        DataMode mode = dataMode.load();
        if (mode == DataMode::PASSIVE) {
            DataChannel dc(0);
            if (dc.start()) {
                dc.sendProbe(serverIp, serverPasvPort.load());
                dc.receiveFile(filename);
                dc.stop();
            }
        }
        else if (mode == DataMode::ACTIVE) {
            DataChannel dc(myActivePort.load());
            if (dc.start()) {
                dc.receiveFile(filename);
                dc.stop();
            }
        }
        else {
            cerr << "425 Can't open data connection: no PORT/PASV negotiated for this RETR" << endl;
        }
    }
}

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
                dataMode = DataMode::PASSIVE;
            }
        }

        if (reply.substr(0, 3) == "150") {
            unsigned short p;
            if (parseEmbeddedPort(reply, p)) serverUploadPort = p;

            string cmdWord, filename;
            {
                lock_guard<mutex> lock(pendingMutex);
                cmdWord = pendingCmdWord;
                filename = pendingArg;
            }
            doDataTransfer(cmdWord, filename);
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

    receiverThread = thread(&ControlChannel::receiverLoop, this);

    string input;
    while (keepRunning) {
        cout << "ftp> ";
        if (!getline(cin, input)) break;
        if (input.empty()) continue;

        size_t sp = input.find(' ');
        string cmdWord = (sp == string::npos) ? input : input.substr(0, sp);
        for (auto& c : cmdWord) c = toupper(c);
        string cmdArg = (sp == string::npos) ? "" : input.substr(sp + 1);

        if (cmdWord == "PORT") {
            unsigned short p;
            if (parsePortArgLocal(cmdArg, p)) {
                myActivePort = p;
                dataMode = DataMode::ACTIVE;
            }
        }

        if (cmdWord == "RETR" && dataMode.load() == DataMode::NONE) {
            autoNegotiateActivePort();
        }

        {
            lock_guard<mutex> lock(pendingMutex);
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
        closesocket(this->tcpSocket);
        this->tcpSocket = INVALID_SOCKET;
    }
}
