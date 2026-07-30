#include "Session.h"

Session::Session() {
	this->isLoggedIn = false;
	this->userName = "";
	this->currentDir = "/";
	this->dataType = "A";      
	this->transferMode = "S";  
	this->renameFrom = "";     
	this->dataMode = DataMode::NONE;
	this->activeIp = "";
	this->activePort = 0;  
	this->passivePort = 0; 
	this->activeDataChannel = nullptr;
}

Session::~Session() {
	//Tự động dọn dẹp DataChannel nếu Session bị hủy mà kênh chưa đóng
	lock_guard<mutex> lock(this->dcMutex);
	if (this->activeDataChannel != nullptr) {
		delete this->activeDataChannel;
		this->activeDataChannel = nullptr;
	}
}

void Session::setLoggedIn(bool logged) { this->isLoggedIn = logged; }
void Session::setUserName(string name) { this->userName = name; }
void Session::setDir(string dir) { this->currentDir = dir; }
void Session::setType(string type) { this->dataType = type; }
void Session::setMode(string mode) { this->transferMode = mode; }
void Session::setRenameFrom(string name) { this->renameFrom = name; }

void Session::setActiveMode(const string& ip, unsigned short port) {
	this->dataMode = DataMode::ACTIVE; 
	this->activeIp = ip; 
	this->activePort = port;
}

void Session::setPassiveMode(unsigned short port) {
	this->dataMode = DataMode::PASSIVE; 
	this->passivePort = port;
}

void Session::setActiveDataChannel(DataChannel* dc) {
	lock_guard<mutex> lock(this->dcMutex);
	if (this->activeDataChannel != nullptr || this->activeDataChannel == dc)
		delete this->activeDataChannel;
	this->activeDataChannel = dc;
}

bool Session::abortActiveTransfer() {
	lock_guard<mutex> lock(this->dcMutex);
	if (this->activeDataChannel != nullptr) {
		this->activeDataChannel->stop(); // closesocket() -> recvfrom/sendto đang block ở thread phụ báo lỗi
		return true;
	}
	return false;
}