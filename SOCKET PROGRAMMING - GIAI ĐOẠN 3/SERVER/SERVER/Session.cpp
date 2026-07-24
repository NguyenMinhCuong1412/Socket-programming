#include "Session.h"

Session::Session() {
	this->isLoggedIn = false;
	this->userName = "";
	this->currentDir = "/";
	this->dataType = "A";     // mặc định ASCII (Basic Level)
	this->transferMode = "S"; // mặc định Stream mode
}

void Session::setLoggedIn(bool logged) { this->isLoggedIn = logged; }
void Session::setUserName(string name) { this->userName = name; }
void Session::setDir(string dir) { this->currentDir = dir; }
void Session::setDataType(string type) { this->dataType = type; }
void Session::setTransferMode(string mode) { this->transferMode = mode; }