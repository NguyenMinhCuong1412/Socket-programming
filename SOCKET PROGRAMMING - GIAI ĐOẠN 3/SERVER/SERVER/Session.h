#pragma once
#include "lib.h"

class Session {
private:
	bool isLoggedIn;
	string userName;
	string currentDir;
	string dataType;     // "A" = ASCII, "I" = Binary
	string transferMode; // "S" = Stream, "B" = Block, "C" = Compressed
public:
	Session();

	bool getLoggedIn() { return this->isLoggedIn; }
	string getUserName() { return this->userName; }
	string getDir() { return this->currentDir; }
	string getDataType() { return this->dataType; }
	string getTransferMode() { return this->transferMode; }

	void setLoggedIn(bool);
	void setUserName(string);
	void setDir(string);
	void setDataType(string);
	void setTransferMode(string);

	~Session() {};
};