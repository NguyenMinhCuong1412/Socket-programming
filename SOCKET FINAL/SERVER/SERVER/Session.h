#pragma once
#include "lib.h"

//Phiên làm việc
class Session {
private:
	bool isLoggedIn;
	string userName;
	string currentDir;
public:
	Session();

	bool getLoggedIn() { return this->isLoggedIn; }
	string getUserName() { return this->userName; }
	string getDir() { return this->currentDir; }

	void setLoggedIn(bool);
	void setUserName(string);
	void setDir(string);

	~Session() {};
};