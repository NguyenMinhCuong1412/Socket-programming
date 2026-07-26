#include "lib.h"

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

    if (inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr) <= 0) {
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
    cout << "200 Connected successfully, ready for commands" << endl;

    char buffer[1024] = { 0 };
    int bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
    if (bytesReceived > 0) cout << "Server: " << buffer << endl;
    else {
        cerr << "421 Service not available, did not receive greeting from server" << endl;
        closesocket(clientSocket);
        WSACleanup();
        return 1;
    }

    //4. Vòng lặp gửi lệnh / nhận phản hồi
    string input;

    while (true) {
        cout << "ftp> ";
        getline(cin, input);
        if (input.empty()) continue;

        send(clientSocket, input.c_str(), (int)input.size(), 0);
        ZeroMemory(buffer, sizeof(buffer));

        bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);

        if (bytesReceived > 0) cout << "Server: " << buffer << endl;
        else if (bytesReceived == 0) {
            cout << "221 Connection closed by remote host" << endl;
            break;
        }
        else {
            cerr << format("426 Connection closed, transfer aborted (WSA error: {})", WSAGetLastError()) << endl;
            break;
        }

        if (input.substr(0, 4) == "QUIT") break;
    }
}