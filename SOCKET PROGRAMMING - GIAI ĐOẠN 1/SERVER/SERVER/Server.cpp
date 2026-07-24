#include <iostream>
#include <format>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using std::cerr, std::cout, std::endl, std::format;

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

    cout << "220 Service ready, listening on port 8080..." << endl;

    //5. Accept Client
    SOCKET clientSocket = accept(serverSocket, NULL, NULL);
    if (clientSocket == INVALID_SOCKET) cerr << format("421 Service not available, accept failed (WSA error: {})", WSAGetLastError()) << endl;
    else {
        cout << "220 Connection accepted, service ready for new user" << endl;

        const char* msg = "Xin chào Client";
        send(clientSocket, msg, (int)strlen(msg), 0);

        closesocket(clientSocket);
    }

    closesocket(serverSocket);
    WSACleanup();
    return 0;
}