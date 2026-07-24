#include "lib.h"
#include "Session.h"
#include "CommandHandler.h"

void parseCommand(const string& raw, string& command, string& argument) {
    string cleaned = raw;
    while (!cleaned.empty() && (cleaned.back() == '\r' || cleaned.back() == '\n'))
        cleaned.pop_back();

    istringstream iss(cleaned);
    iss >> command;
    getline(iss, argument);
    if (!argument.empty() && argument[0] == ' ') argument = argument.substr(1);
    for (auto& c : command) c = toupper(c);
}

int main() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << "421 Service not available, WSAStartup failed" << endl;
        return 1;
    }

    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket == INVALID_SOCKET) {
        cerr << format("421 Service not available, cannot create socket (WSA error: {})", WSAGetLastError()) << endl;
        WSACleanup();
        return 1;
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(CONTROL_PORT);

    if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        cerr << format("421 Service not available, bind failed (WSA error: {})", WSAGetLastError()) << endl;
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
        cerr << format("421 Service not available, listen failed (WSA error: {})", WSAGetLastError()) << endl;
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    cout << "Server listening on port " << CONTROL_PORT << "..." << endl;

    sockaddr_in clientAddr;
    int clientAddrLen = sizeof(clientAddr);
    SOCKET clientSocket = accept(serverSocket, (sockaddr*)&clientAddr, &clientAddrLen);
    if (clientSocket == INVALID_SOCKET) {
        cerr << format("421 Service not available, accept failed (WSA error: {})", WSAGetLastError()) << endl;
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    char clientIpStr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &clientAddr.sin_addr, clientIpStr, INET_ADDRSTRLEN);
    cout << "Client connected from " << clientIpStr << endl;

    string greeting = "220 Service ready\r\n";
    send(clientSocket, greeting.c_str(), (int)greeting.size(), 0);

    Session session;
    CommandHandler handler(clientIpStr);
    char buffer[1024] = { 0 };

    while (true) {
        ZeroMemory(buffer, sizeof(buffer));
        int byteReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);

        if (byteReceived == 0) {
            cout << "Client disconnected." << endl;
            break;
        }
        else if (byteReceived < 0) {
            cerr << format("426 Connection closed, transfer aborted (WSA error: {})", WSAGetLastError()) << endl;
            break;
        }

        string raw(buffer), command, argument;
        parseCommand(raw, command, argument);

        cout << format("Command: {} | Argument: {}", command, argument) << endl;

        string reply = handler.handle(session, command, argument);
        send(clientSocket, reply.c_str(), (int)reply.size(), 0);

        if (command == "QUIT") break;
    }

    closesocket(clientSocket);
    closesocket(serverSocket);
    WSACleanup();
    return 0;
}