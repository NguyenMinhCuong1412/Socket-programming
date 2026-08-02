#pragma once
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <fstream>      //Đọc/Ghi file
#include <format>
#include <cctype>
#include <mutex>        //Làm việc với Data Race (Xung đột dữ liệu) và Critical Section (Vùng tranh chấp) khi có nhiều luồng cùng truy cập vào một vùng nhớ
#include <thread>       //Làm việc với luồng thực thi/tác vụ song song (parallel) hoặc đồng thời (concurrent) - cấp độ hệ điều hành 
#include <atomic>       //Làm việc với các thao tác dữ liệu mức thấp (CPU assembly level) không thể bị ngắt quãng
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

using std::cerr, std::cout,
std::endl, std::format,
std::string, std::cin,
std::stringstream, std::vector,
std::ifstream, std::ofstream,
std::toupper, std::ios, std::atomic;

constexpr unsigned short CONTROL_PORT = 8080;
constexpr unsigned short SERVER_DATA_PORT = 8081; //server bind cổng này để nhận STOR
constexpr unsigned short CLIENT_DATA_PORT = 8082; //client bind cổng này để nhận RETR
constexpr int CHUNK_SIZE = 1024;

enum class ClientDataMode { NONE, ACTIVE, PASSIVE };