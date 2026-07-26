#pragma once
#include <iostream>
#include <string>
#include <sstream>
#include <format>
#include <cctype>
#include <fstream>    //Làm việc với ghi/đọc file
#include <filesystem> //làm việc với ổ đĩa
#include <chrono>     //Làm việc thời gian
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

using std::cerr, std::cout,
std::endl, std::format,
std::string, std::istringstream, 
std::ifstream, std::ofstream;

namespace fs = std::filesystem;
namespace chr = std::chrono;

//constexpr: giúp compile có sẵn dữ liệu khi bắt đầu chạy, ko cần tính toán gây mất thời gian
constexpr unsigned short CONTROL_PORT = 8080;
constexpr unsigned short DATA_PORT = 8081;
constexpr int CHUNK_SIZE = 1024; //Kích thước mỗi gói dữ liệu