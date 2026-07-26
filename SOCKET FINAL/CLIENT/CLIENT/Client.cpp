#include "lib.h"

int main() {
	//0. Khởi tạo môi trường socket
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		cerr << "421 Service unavailable, WSAStartup failed" << endl;
		return 1;
	}

	//1. Tạo socket
	SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (clientSocket == INVALID_SOCKET) {
		cerr << format("421 Service unavailable, cannot create socket (WSA error: {})", WSAGetLastError()) << endl;
		WSACleanup();
		return 1;
	}

	//2. Định danh địa chỉ Server cần kết nối
	sockaddr_in serverAddr;
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(8080);

	if (inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr) <= 0) {
		cerr << "501 Syntax error in parameters or arguments, invalid IP address" << endl;
		closesocket(clientSocket);
		WSACleanup();
		return 1;
	}

	//3. Connect tới Server
	if (connect(clientSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
		cerr << format("421 Service unavailable, cannot connect to server (WSA error: {})", WSAGetLastError()) << endl;
		closesocket(clientSocket);
		WSACleanup();
		return 1;
	}
	cout << "200 Connected successfully, ready for commands" << endl;

	char buffer[1024] = { 0 };
	int byteRecv = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
	if (byteRecv > 0) cout << "Server: " << buffer << endl;
	else {
		cerr << "421 Service unavailable, did not receive greeting from server" << endl;
		closesocket(clientSocket);
		WSACleanup();
		return 1;
	}

	//4. Vòng lặp gửi lệnh / nhận phản hồi
	string input;
	while (true) {
		cout << "ftp> ";

		//Nhận lệnh
		getline(cin, input);
		if (input.empty()) continue;

		//Gửi lệnh cho Server
		send(clientSocket, input.c_str(), (int)input.size(), 0);

		//Làm sạch vùng nhớ để chứa dữ liệu
		ZeroMemory(buffer, sizeof(buffer));

		//Nhận và kiểm tra phản hồi 
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

		if (input.substr(0, 4) == "QUIT") break;
	}

	closesocket(clientSocket);
	WSACleanup();
	return 0;
}