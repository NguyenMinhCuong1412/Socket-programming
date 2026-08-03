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

// Chỉ CONTROL_PORT cần cố định (cổng "well-known" để Client biết trước mà kết nối TCP).
// Cổng UDP cho STOR/APPE/STOU (trước là SERVER_DATA_PORT=8081) và cho RETR khi chưa dùng
// PORT/PASV (trước là CLIENT_DATA_PORT=8082) giờ NGẪU NHIÊN do OS cấp, xem
// ControlChannel::autoNegotiateActivePort() và ControlChannel::parseEmbeddedPort().
constexpr unsigned short CONTROL_PORT = 8080;
constexpr int CHUNK_SIZE = 1024;

enum class ClientDataMode { NONE, ACTIVE, PASSIVE };