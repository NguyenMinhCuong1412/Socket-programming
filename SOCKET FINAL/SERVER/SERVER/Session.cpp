#include "Session.h"

Session::Session() {
	this->isLoggedIn = false;
	this->userName = "";
	this->currentDir = fs::current_path().string();
	this->dataType = "A";
	this->transferMode = "S";

	this->dataSocket = INVALID_SOCKET;
	ZeroMemory(&this->clientDataAddr, sizeof(this->clientDataAddr));
}

void Session::setLogged(bool logged) { this->isLoggedIn = logged; }
void Session::setName(const string& name) { this->userName = name; }
void Session::setDir(const string& dir) { this->currentDir = dir; }
void Session::setType(const string& type) { this->dataType = type; }
void Session::setMode(const string& mode) { this->transferMode = mode; }

void Session::setDataSocket(SOCKET sock) { this->dataSocket = sock; }
void Session::setClientDataAddr(const sockaddr_in& addr) { this->clientDataAddr = addr; }