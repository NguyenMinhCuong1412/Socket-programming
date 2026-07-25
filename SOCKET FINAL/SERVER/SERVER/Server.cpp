#include "lib.h"
#include "Session.h"
#include "CommandHandler.h"

//Hàm xử lý lệnh - tách lệnh
void parseCommand(const string& raw, string& cmd, string& arg) {
	string cleaned = raw;
	while (!cleaned.empty() && (cleaned.back() == '\r' || cleaned.back() == '\n')) cleaned.pop_back();
	istringstream iss(cleaned);
	iss >> cmd;
	getline(iss, arg);
	if (!arg.empty() && arg[0] == ' ') arg = arg.substr(1);
	for (auto& c : cmd) c = toupper(c);
}

int main() {
	//0. Khởi tạo môi trường socket
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		cerr << "421 Service unavailable, WSAStartup failed" << endl;
		return 1;
	}

	//1. Tạo socket 
	SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (serverSocket == INVALID_SOCKET) {
		cerr << format("421 Service unavailable, cannot create socket (WSA error: {})", WSAGetLastError()) << endl;
		WSACleanup();
		return 1;
	}

	//2. Định danh địa chỉ Server
	sockaddr_in serverAddr;
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.s_addr = INADDR_ANY;
	serverAddr.sin_port = htons(8080);

	//3. Bind Socket
	if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
		cerr << format("421 Service unavailable, bind failed (WSA error: {})", WSAGetLastError()) << endl;
		closesocket(serverSocket);
		WSACleanup();
		return 1;
	}

	//4. Listen từ Client
	if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
		cerr << format("421 Service unavailable, listen failed (WSA error: {})", WSAGetLastError()) << endl;
		closesocket(serverSocket);
		WSACleanup();
		return 1;
	}

	cout << "Server listening on port 8080..." << endl;

	//5. Accept Client
	SOCKET clientSocket = accept(serverSocket, NULL, NULL);
	if (clientSocket == INVALID_SOCKET) {
		cerr << format("421 Service unavailable, accept failed (WSA error: {})", WSAGetLastError()) << endl;
		closesocket(serverSocket);
		WSACleanup();
		return 1;
	}
	cout << "Client connected" << endl;

	string greeting = "220 Service ready for new user\r\n";
	send(clientSocket, greeting.c_str(), (int)greeting.size(), 0);

	//6. Vòng lặp gửi phản hồi / nhận lệnh
	Session session;
	CommandHandler cmdHandle;
	char buffer[1024] = { 0 };

	while (true) {
		//Làm sạch vùng nhớ để chứa dữ liệu
		ZeroMemory(buffer, sizeof(buffer));

		//Nhận dữ liệu và kiểm tra
		int byteRecv = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
		if (byteRecv == 0) {
			cout << "Client disconnected" << endl;
			break;
		}
		else if (byteRecv < 0) {
			cerr << format("426 Connection closed, transfer aborted (WSA error: {})", WSAGetLastError()) << endl;
			break;
		}
		
		//Xử lý và in lệnh 
		string raw(buffer), command, argument;
		parseCommand(raw, command, argument);
		cout << format("Command: {} | Argument: {}", command, argument) << endl;

		//Lấy và gửi phản hồi 
		string reply = cmdHandle.handle(session, command, argument);
		send(clientSocket, reply.c_str(), (int)reply.size(), 0);

		if (command == "QUIT") break;
	}

	closesocket(clientSocket);
	closesocket(serverSocket);
	WSACleanup();
	return 0;
}