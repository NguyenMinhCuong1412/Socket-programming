#pragma once
#include "lib.h"

class ControlChannel {
private:
	unsigned short port;
	SOCKET serverSocket;
public:
	ControlChannel(unsigned short);
	~ControlChannel() = default;

	bool start(); 
	void run();    
	void stop();
};