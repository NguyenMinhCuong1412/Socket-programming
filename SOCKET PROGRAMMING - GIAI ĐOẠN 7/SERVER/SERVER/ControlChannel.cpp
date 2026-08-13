#include "ControlChannel.h"
#include "Session.h"
#include "CmdHandler.h"
#include "SessionRegistry.h"

void ControlChannel::handleClient(SOCKET clientSocket, string clientIp) {
    {
        lock_guard<mutex> lock(g_coutMutex);
        cout << "Client connected from " << clientIp << endl;
    }

    SessionRegistry::add(clientSocket, clientIp);
    SessionRegistry::printTable();

    string greeting = "220 Service ready\r\n";
    send(clientSocket, greeting.c_str(), (int)greeting.size(), 0);

    Session session;
    CommandHandler handler;
    handler.setControlSocket(clientSocket);
    handler.setClientIp(clientIp);
    char buffer[1024] = { 0 };
    string streamBuffer = "";
    bool shouldQuit = false;

    while (!shouldQuit) {
        ZeroMemory(buffer, sizeof(buffer));

        int byteRecv = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
        if (byteRecv <= 0) {
            lock_guard<mutex> lock(g_coutMutex);
            if (byteRecv == 0) cout << format("Client {} disconnected", clientIp) << endl;
            else cerr << format("426 Connection closed, transfer aborted (WSA error: {})", WSAGetLastError()) << endl;
            break;
        }

        streamBuffer.append(buffer, byteRecv);

        size_t pos = 0;
        while ((pos = streamBuffer.find('\n')) != string::npos) {
            string line = streamBuffer.substr(0, pos);
            streamBuffer.erase(0, pos + 1);

            string command, argument;
            parseCmd(line, command, argument);
            if (command.empty()) continue;

            {
                lock_guard<mutex> lock(g_coutMutex);
                cout << format("[{}] Command: {} | Argument: {}", clientIp, command, argument) << endl;
            }

            string reply = handler.handle(session, command, argument);
            if (!reply.empty()) send(clientSocket, reply.c_str(), (int)reply.size(), 0);

            SessionRegistry::update(clientSocket, session.getUserName(), session.getLoggedIn(), session.getDir(), command);

            if (command == "QUIT") {
                shouldQuit = true;
                break;
            }
        }
    }

    SessionRegistry::remove(clientSocket);
    SessionRegistry::printTable();

    closesocket(clientSocket);
}

void ControlChannel::adminConsoleLoop() {
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
	this->tcpPort = port;
    this->tcpSocket = INVALID_SOCKET;
}

bool ControlChannel::start() {
    this->tcpSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (this->tcpSocket == INVALID_SOCKET) {
        cerr << format("421 Service not available, cannot create socket (WSA error: {})", WSAGetLastError()) << endl;
        return false;
    }

    sockaddr_in serverAddrTcp = {};
    serverAddrTcp.sin_family = AF_INET;
    serverAddrTcp.sin_addr.s_addr = INADDR_ANY;
    serverAddrTcp.sin_port = htons(this->tcpPort);

    if (bind(this->tcpSocket, (sockaddr*)&serverAddrTcp, sizeof(serverAddrTcp)) == SOCKET_ERROR) {
        cerr << format("421 Service not available, bind failed (WSA error: {})", WSAGetLastError()) << endl;
        closesocket(this->tcpSocket);
        this->tcpSocket = INVALID_SOCKET;
        return false;
    }

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
    thread(&ControlChannel::adminConsoleLoop, this).detach();
    cout << "Type 'sessions' anytime to view the active session table (type 'help' for more)." << endl;

    while (true) {
        sockaddr_in clientAddr = {};
        int clientAddrLen = sizeof(clientAddr);

        SOCKET clientSocket = accept(this->tcpSocket, (sockaddr*)&clientAddr, &clientAddrLen);
        if (clientSocket == INVALID_SOCKET) {
            if (this->tcpSocket == INVALID_SOCKET) break;
            cerr << format("421 Service not available, accept failed (WSA error: {})", WSAGetLastError()) << endl;
            continue;
        }

        char clientIpStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, clientIpStr, INET_ADDRSTRLEN);

        thread(&ControlChannel::handleClient, this, clientSocket, string(clientIpStr)).detach();
    }
}

void ControlChannel::stop() {
    if (this->tcpSocket != INVALID_SOCKET) {
        closesocket(this->tcpSocket);
        this->tcpSocket = INVALID_SOCKET;
    }
}