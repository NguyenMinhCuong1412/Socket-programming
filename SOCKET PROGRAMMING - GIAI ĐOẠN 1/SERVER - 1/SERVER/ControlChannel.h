#pragma once
#include "lib.h"

class ControlChannel {
private:
	unsigned short port;
	SOCKET serverSocket;
public:
	ControlChannel(unsigned short);
	~ControlChannel() = default;

	bool start();  // socket + bind + listen
	void run();    
	void stop();
};