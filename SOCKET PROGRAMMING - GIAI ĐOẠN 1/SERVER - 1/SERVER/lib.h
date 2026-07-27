#pragma once
#include <iostream>
#include <format>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using std::cerr, std::cout, std::endl, std::format;

constexpr unsigned short CONTROL_PORT = 8080;