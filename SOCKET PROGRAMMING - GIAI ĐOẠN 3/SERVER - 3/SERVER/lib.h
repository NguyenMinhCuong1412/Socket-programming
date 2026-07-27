#pragma once
#include <iostream>
#include <string>
#include <sstream>
#include <fstream>     //Đọc/Ghi file
#include <format>
#include <cctype>
#include <ctime>
#include <filesystem>  //Làm việc với hệ thống file
#include <chrono>      //Làm việc với thời gian 
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using std::cerr, std::cout, 
std::endl, std::format,
std::string, std::istringstream,
std::ifstream, std::ofstream,
std::toupper, std::ios;

namespace fs = std::filesystem;
namespace chr = std::chrono;

constexpr unsigned short CONTROL_PORT = 8080;
constexpr unsigned short SERVER_DATA_PORT = 8081; // server bind cổng này để nhận STOR
constexpr unsigned short CLIENT_DATA_PORT = 8082; // client bind cổng này để nhận RETR
constexpr int CHUNK_SIZE = 1024;