#include "Session.h"

Session::Session() {
	this->isLoggedIn = false;
	this->userName = "";
	this->currentDir = "/";
}

void Session::setLogged(bool logged) { this->isLoggedIn = logged; }
void Session::setName(string name) { this->userName = name; }
void Session::setDir(string dir) { this->currentDir = dir; }