#pragma once
#include <iostream>
#include <string>
#include <sstream>
#include <format>
#include <cctype>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using std::cerr, std::cout,
std::endl, std::format,
std::string, std::istringstream,
std::cin, std::ifstream, std::ofstream;
namespace fs = std::filesystem;

constexpr unsigned short CONTROL_PORT = 8080;
constexpr unsigned short SERVER_DATA_PORT = 2121;
constexpr unsigned short CLIENT_DATA_PORT = 3131;
constexpr int CHUNK_SIZE = 1024;