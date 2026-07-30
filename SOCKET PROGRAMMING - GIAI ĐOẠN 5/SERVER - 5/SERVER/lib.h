#pragma once
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <fstream>      //Đọc/Ghi file
#include <format>
#include <cctype>
#include <ctime>
#include <system_error> //Làm việc với mã lỗi hệ thống
#include <filesystem>   //Làm việc với hệ thống file
#include <chrono>       //Làm việc với thời gian 
#include <mutex>        //Làm việc với Data Race (Xung đột dữ liệu) và Critical Section (Vùng tranh chấp) khi có nhiều luồng cùng truy cập vào một vùng nhớ
#include <thread>       //Làm việc với luồng thực thi/tác vụ song song (parallel) hoặc đồng thời (concurrent) - cấp độ hệ điều hành 
#include <atomic>       //Làm việc với các thao tác dữ liệu mức thấp (CPU assembly level) không thể bị ngắt quãng
#include <memory>       //Làm việc với bộ nhớ Dynamic (Heap memory) và vòng đời của đối tượng (Object Lifetime)
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

using std::cerr, std::cout,
std::endl, std::format,
std::string, std::istringstream,
std::ostringstream, std::stringstream,
std::ifstream, std::ofstream,
std::toupper, std::ios, 
std::tm, std::error_code,
std::mutex, std::lock_guard,
std::thread, std::vector,
std::shared_ptr, std::make_shared;;

namespace fs = std::filesystem;
namespace chr = std::chrono;

constexpr unsigned short CONTROL_PORT = 8080;
constexpr unsigned short SERVER_DATA_PORT = 8081; //server bind cổng này để nhận STOR
constexpr unsigned short CLIENT_DATA_PORT = 8082; //client bind cổng này để nhận RETR
constexpr int CHUNK_SIZE = 1024;

//Thư mục làm việc hiện tại lúc server khởi động (working directory của process).
inline const fs::path SERVER_ROOT = fs::current_path() / "server_root";

//Tránh log của nhiều thread (nhiều client) in xen kẽ, lẫn lộn ra console
inline mutex g_coutMutex;