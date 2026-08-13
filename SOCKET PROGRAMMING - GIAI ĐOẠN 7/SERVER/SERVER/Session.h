#pragma once
#include "lib.h"
#include "DataChannel.h"

class Session {
private:
	bool isLoggedIn;
	string userName;
	string currentDir;
	string dataType;
	string transferMode;
	string renameFrom;
	DataMode dataMode;
	string activeIp;
	unsigned short activePort;
	unsigned short passivePort;
	bool isAborted;
	mutex dcMutex;
	DataChannel* activeDataChannel;
public:
	Session();
	~Session();

	bool getLoggedIn() const { return this->isLoggedIn; }
	string getUserName() const { return this->userName; }
	string getDir() const { return this->currentDir; }
	string getType() const { return this->dataType; }
	string getMode() const { return this->transferMode; }
	string getRenameFrom() const { return this->renameFrom; }
	DataMode getDataMode() const { return this->dataMode; }
	string getActiveIp() const { return this->activeIp; }
	unsigned short getActivePort() const { return this->activePort; }
	unsigned short getPassivePort() const { return this->passivePort; }

	void setLoggedIn(bool);
	void setUserName(string);
	void setDir(string);
	void setType(string);
	void setMode(string);
	void setRenameFrom(string);
	void setActiveMode(const string&, unsigned short);
	void setPassiveMode(unsigned short);
	void resetDataMode();
	void setActiveDataChannel(DataChannel*);
	bool abortActiveTransfer();
	bool isTransferAborted();
	void setTransferAborted(bool);
};