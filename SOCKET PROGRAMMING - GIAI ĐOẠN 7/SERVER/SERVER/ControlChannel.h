#pragma once
#include "lib.h"

class ControlChannel {
private:
	unsigned short tcpPort;
	SOCKET tcpSocket;

	void handleClient(SOCKET clientSocket, string clientIp);
	void adminConsoleLoop();
public:
	ControlChannel(unsigned short);
	~ControlChannel() = default;

	bool start();
	void run();
	void stop();
};