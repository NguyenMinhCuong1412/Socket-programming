#include "lib.h"
#include "DataChannel.h"

int main() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << "421 Service not available, WSAStartup failed" << endl;
        return 1;
    }

    SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (clientSocket == INVALID_SOCKET) {
        cerr << format("421 Service not available, cannot create socket (WSA error: {})", WSAGetLastError()) << endl;
        WSACleanup();
        return 1;
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(CONTROL_PORT);

    string serverIp = "127.0.0.1";
    if (inet_pton(AF_INET, serverIp.c_str(), &serverAddr.sin_addr) <= 0) {
        cerr << "501 Syntax error in parameters, invalid IP address" << endl;
        closesocket(clientSocket);
        WSACleanup();
        return 1;
    }

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

    string input;
    while (true) {
        cout << "ftp> ";
        getline(cin, input);
        if (input.empty()) continue;

        send(clientSocket, input.c_str(), (int)input.size(), 0);
        ZeroMemory(buffer, sizeof(buffer));

        bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);

        if (bytesReceived > 0) {
            string reply(buffer);
            cout << "Server: " << reply << endl;

            // Sau khi server báo "150", client thực hiện phần dữ liệu tương ứng
            if (reply.substr(0, 3) == "150") {
                if (input.substr(0, 4) == "STOR") {
                    string filename = input.substr(5);
                    DataChannel channel(0);
                    if (channel.open()) {
                        channel.sendFile(filename, serverIp, SERVER_DATA_PORT);
                        channel.close();
                    }
                }
                else if (input.substr(0, 4) == "RETR") {
                    string filename = input.substr(5);
                    DataChannel channel(CLIENT_DATA_PORT);
                    if (channel.open()) {
                        channel.receiveFile(filename);
                        channel.close();
                    }
                }
                // Đọc phản hồi "226 Transfer complete" sau khi transfer xong
                ZeroMemory(buffer, sizeof(buffer));
                bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
                if (bytesReceived > 0) cout << "Server: " << buffer << endl;
            }
        }
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

    closesocket(clientSocket);
    WSACleanup();
    return 0;
}