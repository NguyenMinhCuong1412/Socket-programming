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
	~Session() = default;

	bool getLoggedIn() const { return this->isLoggedIn; }
	string getUserName() const { return this->userName; }
	string getDir() const { return this->currentDir; }

	void setLoggedIn(bool);
	void setUserName(string);
	void setDir(string);
};