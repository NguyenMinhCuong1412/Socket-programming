#pragma once
#include <iostream>
#include <string>
#include <sstream>
#include <format>
#include <cctype>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using std::cerr, std::cout, 
std::endl, std::format,
std::string, std::istringstream,
std::toupper;

constexpr unsigned short CONTROL_PORT = 8080;