#pragma once
#include "lib.h"

class ControlChannel {
private:
	unsigned short tcpPort; //Port TCP của Server
	string serverIp;        //IP của Server
	SOCKET clientSocket;    
public:
	ControlChannel(unsigned short, string);
	~ControlChannel() = default;

	bool start();  
	void run();
	void stop();
};