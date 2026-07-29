#pragma once
#include <iostream>
#include <string>
#include <fstream>
#include <cctype>
#include <format>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

using std::cerr, std::cout,
std::endl, std::format,
std::string, std::cin,
std::ifstream, std::ofstream,
std::toupper, std::ios;

constexpr unsigned short CONTROL_PORT = 8080;
constexpr unsigned short SERVER_DATA_PORT = 8081; // server bind cổng này để nhận STOR
constexpr unsigned short CLIENT_DATA_PORT = 8082; // client bind cổng này để nhận RETR
constexpr int CHUNK_SIZE = 1024;