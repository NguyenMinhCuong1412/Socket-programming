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
    SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (clientSocket == INVALID_SOCKET) {
        cerr << format("421 Service not available, cannot create socket (WSA error: {})", WSAGetLastError()) << endl;
        WSACleanup();
        return 1;
    }

    //2. Định danh địa chỉ Server cần kết nối tới
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(8080);

    if (inet_pton(AF_INET, "192.168.0.3", &serverAddr.sin_addr) <= 0) {
        cerr << "501 Syntax error in parameters, invalid IP address" << endl;
        closesocket(clientSocket);
        WSACleanup();
        return 1;
    }

    //3. Kết nối đến Server
    if (connect(clientSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        cerr << format("421 Service not available, cannot connect to server (WSA error: {})", WSAGetLastError()) << endl;
        closesocket(clientSocket);
        WSACleanup();
        return 1;
    }

    //4. Nhận dữ liệu từ Server 
    char buffer[1024] = { 0 };
    int bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);

    if (bytesReceived > 0) cout << "Server: " << buffer << endl;
    else if (bytesReceived == 0) cout << "221 Connection closed by remote host" << endl;
    else cerr << format("426 Connection closed, transfer aborted (WSA error: {}", WSAGetLastError()) << endl;

    closesocket(clientSocket);
    WSACleanup();
    return 0;
}