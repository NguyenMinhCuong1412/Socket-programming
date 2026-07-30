#include "Session.h"
#include "DataChannel.h"

Session::Session() {
	this->isLoggedIn = false;
	this->userName = "";
	this->currentDir = "/";
	this->dataType = "A";      //A = ASCII, I = IMAGE/BINARY
	this->transferMode = "S";  //S = STREAM, B = BLOCK, C = COMPRESSED
	this->renameFrom = "";     //Tên (logical) đang chờ RNTO hoàn tất; rỗng = không có RNFR đang chờ
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
	this->activeDataChannel = dc;
}

bool Session::abortActiveTransfer() {
	lock_guard<mutex> lock(this->dcMutex);
	if (this->activeDataChannel != nullptr) {
		this->activeDataChannel->stop(); // closesocket() -> recvfrom/sendto đang block ở thread phụ báo lỗi ngay
		return true;
	}
	return false;
}