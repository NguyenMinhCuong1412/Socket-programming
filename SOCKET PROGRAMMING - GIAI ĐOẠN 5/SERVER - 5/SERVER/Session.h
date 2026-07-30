#pragma once
#include "lib.h"

class DataChannel; //forward declare: khai báo trước

enum class DataMode { NONE, ACTIVE, PASSIVE };

//Phiên làm việc - MỖI CLIENT có 1 instance riêng (tạo trong thread riêng của client đó)
class Session {
private:
	bool isLoggedIn;
	string userName;
	string currentDir;
	string dataType;
	string transferMode;
	string renameFrom;  

	DataMode dataMode = DataMode::NONE;
	string   activeIp;              //ACTIVE: IP client gửi qua lệnh PORT
	unsigned short activePort = 0;  //ACTIVE: port client gửi qua lệnh PORT
	unsigned short passivePort = 0; //PASSIVE: port server tự chọn, đã thông báo qua PASV
	mutex dcMutex;
	DataChannel* activeDataChannel = nullptr;
public:
	Session();
	~Session() = default;

	bool getLoggedIn() const { return this->isLoggedIn; }
	string getUserName() const { return this->userName; }
	string getDir() const { return this->currentDir; }
	string getType() const { return this->dataType; }
	string getMode() const { return this->transferMode; }
	string getRenameFrom() const { return this->renameFrom; }
	DataMode getDataMode() const { return dataMode; }
	string getActiveIp() const { return activeIp; }
	unsigned short getActivePort() const { return activePort; }
	unsigned short getPassivePort() const { return passivePort; }

	void setLoggedIn(bool);
	void setUserName(string);
	void setDir(string);
	void setType(string);
	void setMode(string);
	void setRenameFrom(string);
	void setActiveMode(const string&, unsigned short);
	void setPassiveMode(unsigned short);
	void setActiveDataChannel(DataChannel*);
	bool abortActiveTransfer();
};