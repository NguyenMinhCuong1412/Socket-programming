#include "ControlChannel.h"
#include "DataChannel.h"

ControlChannel::ControlChannel(unsigned short port, string IP) {
    this->serverTcpPort = port;
    this->serverIp = IP;
    this->tcpSocket = INVALID_SOCKET;
    this->currentDir = "/";
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

    sockaddr_in serverAddr = {};
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

fs::path ControlChannel::resolvePath(const string& arg) {
    fs::path logical = (!arg.empty() && arg[0] == '/')
        ? fs::path(arg)
        : fs::path(this->currentDir) / arg;

    string normStr = logical.lexically_normal().generic_string();
    if (normStr.empty()) normStr = "/";
    if (normStr[0] != '/') return fs::path();

    fs::path relativePart = (normStr == "/") ? fs::path() : fs::path(normStr.substr(1));
    return fs::weakly_canonical(CLIENT_ROOT / relativePart);
}


void ControlChannel::doDataTransfer(const string& cmdWord, const string& filename, uintmax_t totalSize) {
    string localPath = resolvePath(filename).string();

    if (cmdWord == "STOR" || cmdWord == "STOU" || cmdWord == "APPE") {
        totalSize = fs::file_size(localPath);
        unsigned short destPort = (dataMode.load() == DataMode::PASSIVE) ? serverPasvPort.load() : serverUploadPort.load();
        DataChannel dc(0);
        activeDataChannel.store(&dc);
        if (dc.start()) {
            dc.sendFile(localPath, serverIp, destPort, totalSize, isAsciiMode.load());
            dc.stop();
        }
        activeDataChannel.store(nullptr);
    }
    else if (cmdWord == "RETR" || cmdWord == "LIST" || cmdWord == "NLST") {
        DataMode mode = dataMode.load();

        bool isList = (cmdWord == "LIST" || cmdWord == "NLST");
        string targetFile = localPath;
        if (isList) {
            auto ms = chr::duration_cast<chr::milliseconds>(chr::system_clock::now().time_since_epoch()).count();
            string tempFileName = format(".tmp_list_{}.tmp", ms);
            targetFile = (CLIENT_ROOT / tempFileName).string();
            int counter = 0;
            while (fs::exists(targetFile)) {
                counter++;
                targetFile = (CLIENT_ROOT / format(".tmp_list_{}_{}.tmp", ms, counter)).string();
            }
        }

        if (mode == DataMode::PASSIVE) {
            DataChannel dc(0);
            activeDataChannel.store(&dc);
            if (dc.start()) {
                dc.sendProbe(serverIp, serverPasvPort.load());
                dc.receiveFile(targetFile, totalSize, false, isAsciiMode.load());
                dc.stop();
            }
            activeDataChannel.store(nullptr);
        }
        else if (mode == DataMode::ACTIVE) {
            DataChannel dc(myActivePort.load());
            activeDataChannel.store(&dc);
            if (dc.start()) {
                dc.receiveFile(targetFile, totalSize, false, isAsciiMode.load());
                dc.stop();
            }
            activeDataChannel.store(nullptr);
        }
        else {
            cerr << "425 Can't open data connection: no PORT/PASV negotiated" << endl;
        }

        if (isList) {
            if (fs::exists(targetFile)) {
                ifstream ifs(targetFile);
                if (ifs) {
                    string line;
                    while (getline(ifs, line)) {
                        cout << "      " << line << "\n";
                    }
                    ifs.close();
                }
                error_code ec;
                fs::remove(targetFile, ec);
            }
        }
    }
    dataMode = DataMode::NONE;
}

void ControlChannel::receiverLoop() {
    char buffer[1024];
    string streamBuffer = "";

    while (keepRunning) {
        ZeroMemory(buffer, sizeof(buffer));
        int byteRecv = recv(tcpSocket, buffer, sizeof(buffer) - 1, 0);

        if (byteRecv <= 0) {
            if (keepRunning) {
                if (byteRecv == 0) cout << "\n221 Connection closed by remote host" << endl;
                else cerr << format("\n426 Connection closed, transfer aborted (WSA error: {})", WSAGetLastError()) << endl;
            }
            keepRunning = false;
            {
                lock_guard<mutex> lock(replyMutex);
                awaitingReply = false;
            }
            replyCv.notify_one();
            break;
        }

        streamBuffer.append(buffer, byteRecv);

        size_t pos = 0;
        while ((pos = streamBuffer.find('\n')) != string::npos) {
            string reply = streamBuffer.substr(0, pos);
            streamBuffer.erase(0, pos + 1);
            while (!reply.empty() && (reply.back() == '\r' || reply.back() == '\n')) reply.pop_back();

            if (reply.empty()) continue;

            string code = (reply.size() >= 3) ? reply.substr(0, 3) : "";
            bool isAnyStatusCode = (reply.size() >= 3 && isdigit((unsigned char)reply[0]) &&
                                 isdigit((unsigned char)reply[1]) && isdigit((unsigned char)reply[2]));
            bool isFinalStatusCode = isAnyStatusCode && (reply.size() == 3 || reply[3] == ' ');

            if (isAnyStatusCode) {
                string displayReply = reply;
                if (displayReply.size() > 3 && displayReply[3] == '-') displayReply[3] = ' ';
                if (awaitingReply) {
                    cout << "Server: " << displayReply << endl;
                } else {
                    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
                    string msg = "\r" + string(70, ' ') + "\rServer: " + displayReply + "\n\nftp> ";
                    DWORD written;
                    WriteConsoleA(hOut, msg.c_str(), msg.length(), &written, NULL);
                }
            } else {
                string cleanReply = reply;
                size_t start = cleanReply.find_first_not_of(" \t");
                if (start != string::npos) cleanReply = cleanReply.substr(start);
                else cleanReply = "";
                if (awaitingReply) {
                    cout << "      " << cleanReply << endl;
                } else {
                    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
                    string msg = "\r" + string(70, ' ') + "\r      " + cleanReply + "\n\nftp> ";
                    DWORD written;
                    WriteConsoleA(hOut, msg.c_str(), msg.length(), &written, NULL);
                }
            }

            if (code == "227") {
                unsigned short p;
                if (parsePasvReply(reply, p)) {
                    serverPasvPort = p;
                    dataMode = DataMode::PASSIVE;
                }
            }

            if (code == "150") {
                unsigned short p;
                if (parseEmbeddedPort(reply, p)) serverUploadPort = p;

                uintmax_t totalSize = 0;
                size_t startSize = reply.find("(");
                size_t endSize = reply.find(" bytes)");
                if (startSize != string::npos && endSize != string::npos && endSize > startSize) {
                    try { totalSize = stoull(reply.substr(startSize + 1, endSize - startSize - 1)); }
                    catch (...) {}
                }

                string cmdWord, filename;
                {
                    lock_guard<mutex> lock(pendingMutex);
                    cmdWord = pendingCmdWord;
                    filename = pendingArg;
                }

                if (cmdWord == "LIST" || cmdWord == "NLST") {
                    this->doDataTransfer(cmdWord, filename, totalSize);
                } else {
                    thread([this, cmdWord, filename, totalSize]() {
                        this->doDataTransfer(cmdWord, filename, totalSize);
                    }).detach();
                }

                cout << endl;
                {
                    lock_guard<mutex> lock(replyMutex);
                    awaitingReply = false;
                }
                replyCv.notify_one();
            }
            else if (isFinalStatusCode) {
                if (code == "426" || code == "225") {
                    DataChannel* dc = activeDataChannel.load();
                    if (dc) dc->stop();
                }

                if (awaitingReply) cout << endl;
                {
                    lock_guard<mutex> lock(replyMutex);
                    awaitingReply = false;
                }
                replyCv.notify_one();
            }
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
        cout << "ftp> " << std::flush;
        if (!getline(cin, input)) break;
        if (input.empty()) continue;

        size_t sp = input.find(' ');
        string cmdWord = (sp == string::npos) ? input : input.substr(0, sp);
        for (auto& c : cmdWord) c = toupper(c);
        string cmdArg = (sp == string::npos) ? "" : input.substr(sp + 1);

        if (cmdWord == "TYPE") {
            if (cmdArg == "A" || cmdArg == "a") isAsciiMode.store(true);
            else if (cmdArg == "I" || cmdArg == "i") isAsciiMode.store(false);
        }

        if (cmdWord == "PORT") {
            unsigned short p;
            if (parsePortArgLocal(cmdArg, p)) {
                myActivePort = p;
                dataMode = DataMode::ACTIVE;
            }
        }

        if ((cmdWord == "RETR" || cmdWord == "LIST" || cmdWord == "NLST" || cmdWord == "STOR" || cmdWord == "STOU" || cmdWord == "APPE") && dataMode.load() == DataMode::NONE) {
            cerr << "425 Can't open data connection: send PORT or PASV before using this command\n";
            cout << endl;
            continue;
        }

        if (cmdWord == "STOR" || cmdWord == "STOU" || cmdWord == "APPE") {
            if (cmdArg.empty()) {
                cerr << "501 Syntax error in parameters\n";
                cout << endl;
                continue;
            }
            string localPath = resolvePath(cmdArg).string();
            if (!fs::exists(localPath) || !fs::is_regular_file(localPath)) {
                cerr << "550 File unavailable, local file not found: " << cmdArg << "\n";
                cout << endl;
                continue;
            }
        }

        {
            lock_guard<mutex> lock(pendingMutex);
            pendingCmdWord = cmdWord;
            pendingArg = cmdArg;
        }

        {
            lock_guard<mutex> lock(replyMutex);
            awaitingReply = true;
        }

        string cmdLine = input + "\r\n";
        send(tcpSocket, cmdLine.c_str(), (int)cmdLine.size(), 0);

        bool isQuit = (cmdWord == "QUIT");

        {
            unique_lock<mutex> lock(replyMutex);
            replyCv.wait(lock, [this] { return !awaitingReply || !keepRunning; });
        }

        if (isQuit) { keepRunning = false; break; }
    }

    stop();
}

void ControlChannel::stop() {
    keepRunning = false;
    if (this->tcpSocket != INVALID_SOCKET) {
        closesocket(this->tcpSocket);
        this->tcpSocket = INVALID_SOCKET;
    }
    if (receiverThread.joinable() && receiverThread.get_id() != std::this_thread::get_id()) {
        receiverThread.join();
    }
}
