#pragma once
#include "lib.h"

//Phiên làm việc
class Session {
private:
	bool isLoggedIn;
	string userName;
	string currentDir;
	string dataType;
	string transferMode; 
public:
	Session();
	~Session() = default;

	bool getLoggedIn() const { return this->isLoggedIn; }
	string getUserName() const { return this->userName; }
	string getDir() const { return this->currentDir; }
	string getType() const { return this->dataType; }
	string getMode() const { return this->transferMode; }

	void setLoggedIn(bool);
	void setUserName(string);
	void setDir(string);
	void setType(string);
	void setMode(string);
};