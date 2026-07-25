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

	bool getLogged() { return this->isLoggedIn; }
	string getName() { return this->userName; }
	string getDir() { return this->currentDir; }

	void setLogged(bool);
	void setName(string);
	void setDir(string);

	~Session() {};
};