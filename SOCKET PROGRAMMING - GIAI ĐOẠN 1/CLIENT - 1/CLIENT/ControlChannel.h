#pragma once
#include "lib.h"

class ControlChannel {
private:
	unsigned short port;
	string serverIp;
	SOCKET clientSocket;
public:
	ControlChannel(unsigned short, string);
	~ControlChannel() = default;

	bool start();  
	void run();
	void stop();
};