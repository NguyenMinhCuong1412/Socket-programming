#include "Session.h"

Session::Session() {
	this->isLoggedIn = false;
	this->userName = "";
	this->currentDir = "/";
	this->dataType = "A";      //A = ASCII, I = IMAGE/BINARY
	this->transferMode = "S";  //S = STREAM, B = BLOCK, C = COMPRESSED
	this->renameFrom = "";
}

void Session::setLoggedIn(bool logged) { this->isLoggedIn = logged; }
void Session::setUserName(string name) { this->userName = name; }
void Session::setDir(string dir) { this->currentDir = dir; }
void Session::setType(string type) { this->dataType = type; }
void Session::setMode(string mode) { this->transferMode = mode; }
void Session::setRenameFrom(string name) { this->renameFrom = name; }