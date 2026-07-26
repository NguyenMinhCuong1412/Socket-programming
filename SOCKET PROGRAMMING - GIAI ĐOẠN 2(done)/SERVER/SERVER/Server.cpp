#include "lib.h"
#include "Session.h"
#include "CommandHandler.h"

void parseCommand(const string& raw, string& command, string& argument) {
    string cleaned = raw;
    while (!cleaned.empty() && (cleaned.back() == '\r' || cleaned.back() == '\n')) cleaned.pop_back();

    istringstream iss(cleaned);
    iss >> command;
    getline(iss, argument);
    if (!argument.empty() && argument[0] == ' ') argument = argument.substr(1);
    for (auto& c : command) c = toupper(c);
}

int main() {
    //0. Khởi tạo môi trường socket
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << "421 Service not available, WSAStartup failed" << endl;
        return 1;
    }

    //1. Tạo socket
    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket == INVALID_SOCKET) {
        cerr << format("421 Service not available, cannot create socket (WSA error: {})", WSAGetLastError()) << endl;
        WSACleanup();
        return 1;
    }

    //2. Định danh địa chỉ Server
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(8080);

    //3. Bind socket
    if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        cerr << format("421 Service not available, bind failed (WSA error: {})", WSAGetLastError()) << endl;
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    //4. Lắng nghe từ Client
    if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
        cerr << format("421 Service not available, listen failed (WSA error: {})", WSAGetLastError()) << endl;
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    cout << "Server listening on port 8080..." << endl;

    //5. Accept Client
    SOCKET clientSocket = accept(serverSocket, NULL, NULL);
    if (clientSocket == INVALID_SOCKET) {
        cerr << format("421 Service not available, accept failed (WSA error: {})", WSAGetLastError()) << endl;
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }
    cout << "Client connected." << endl;

    string greeting = "220 Service ready\r\n";
    send(clientSocket, greeting.c_str(), (int)greeting.size(), 0);

    //6. Vòng lặp gửi phản hồi / nhận lệnh
    Session session;
    CommandHandler handler;
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