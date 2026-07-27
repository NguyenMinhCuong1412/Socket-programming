#include "ControlChannel.h"
#include "DataChannel.h"

ControlChannel::ControlChannel(unsigned short port, string IP) {
    this->tcpPort = port;
    this->serverIp = IP;
    this->clientSocket = INVALID_SOCKET;
}

bool ControlChannel::start() {
    //Tạo socket
    this->clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (this->clientSocket == INVALID_SOCKET) {
        cerr << format("421 Service not available, cannot create socket (WSA error: {})", WSAGetLastError()) << endl;
        return false;
    }

    //Định danh địa chỉ Server cần kết nối tới
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(this->tcpPort);

    if (inet_pton(AF_INET, (this->serverIp).c_str(), &serverAddr.sin_addr) <= 0) {
        cerr << "501 Syntax error in parameters, invalid IP address" << endl;
        closesocket(this->clientSocket);
        this->clientSocket = INVALID_SOCKET;
        return false;
    }

    //Kết nối đến Server
    if (connect(this->clientSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        cerr << format("421 Service not available, cannot connect to server (WSA error: {})", WSAGetLastError()) << endl;
        closesocket(clientSocket);
        this->clientSocket = INVALID_SOCKET;
        return false;
    }

    cout << "200 Connected successfully, ready for commands" << endl;

    return true;
}

void ControlChannel::run() {
    //Nhận dữ liệu từ Server 
    char buffer[1024] = { 0 };
    int byteRecv = recv(this->clientSocket, buffer, sizeof(buffer) - 1, 0);

    if (byteRecv > 0) cout << "Server: " << buffer << endl;
    else {
        cerr << "421 Service not available, did not receive greeting from server" << endl;
        closesocket(clientSocket);
        this->clientSocket = INVALID_SOCKET;
        WSACleanup();
        return;
    }

    //Vòng lặp gửi lệnh/nhận phản hồi
    string input;
    while (true) {
        cout << "ftp> ";
        getline(cin, input);
        if (input.empty()) continue;

        send(clientSocket, input.c_str(), (int)input.size(), 0);
        ZeroMemory(buffer, sizeof(buffer));

        byteRecv = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);

        if (byteRecv > 0) {
            string reply(buffer);
            cout << "Server: " << reply << endl;

            if (reply.substr(0, 3) == "150") {
                string cmdWord = input.substr(0, 4);
                for (auto& c : cmdWord) c = toupper(c);
                string filename = input.size() > 5 ? input.substr(5) : "";

                if (cmdWord == "STOR") {
                    DataChannel dc(0);
                    if (dc.start()) {
                        dc.sendFile(filename, this->serverIp, SERVER_DATA_PORT);
                        dc.stop();
                    }
                }
                else if (cmdWord == "RETR") {
                    DataChannel dc(CLIENT_DATA_PORT);
                    if (dc.start()) {
                        dc.receiveFile(filename);
                        dc.stop();
                    }
                }

                ZeroMemory(buffer, sizeof(buffer));
                byteRecv = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
                if (byteRecv > 0) cout << "Server: " << buffer << endl;
                else if (byteRecv == 0) {
                    cout << "221 Connection closed by remote host" << endl;
                    break;
                }
                else {
                    cerr << format("426 Connection closed, transfer aborted (WSA error: {})", WSAGetLastError()) << endl;
                    break;
                }
            }
        }
        else if (byteRecv == 0) {
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

void ControlChannel::stop() {
    if (this->clientSocket != INVALID_SOCKET) {
        closesocket(this->clientSocket);
        this->clientSocket = INVALID_SOCKET;
    }
}