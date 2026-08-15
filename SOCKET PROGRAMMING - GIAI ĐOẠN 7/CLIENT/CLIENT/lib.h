// ======================================================================
// lib.h — FILE TIÊU ĐỀ CHUNG (COMMON HEADER) CỦA FTP CLIENT
//    Tập trung toàn bộ các #include thư viện chuẩn C++, thư viện
//    Winsock2, Windows API, và các khai báo dùng chung (hằng số,
//    enum, đường dẫn gốc) cho toàn bộ chương trình Client.
//    Mọi file .h/.cpp khác trong Client đều include file này.
// ======================================================================
#pragma once

// === Thư viện chuẩn C++ ===
#include <iostream>       // cout, cerr, cin — nhập xuất console
#include <format>         // std::format — định dạng chuỗi kiểu Python (C++20)
#include <vector>         // std::vector — mảng động
#include <string>         // std::string — chuỗi ký tự
#include <sstream>        // stringstream, istringstream, ostringstream — xử lý chuỗi dạng luồng
#include <fstream>        // ifstream, ofstream — đọc/ghi file
#include <cctype>         // toupper, isdigit — xử lý ký tự
#include <ctime>          // std::tm, localtime_s — thao tác thời gian
#include <chrono>         // chr::steady_clock, chr::system_clock — đo thời gian, hẹn giờ
#include <mutex>          // mutex, lock_guard, unique_lock — đồng bộ hóa đa luồng
#include <condition_variable> // condition_variable — chờ/thông báo giữa các thread
#include <thread>         // std::thread — tạo và quản lý luồng
#include <atomic>         // std::atomic — biến nguyên tử, an toàn khi truy cập từ nhiều thread
#include <memory>         // shared_ptr, make_shared — quản lý bộ nhớ thông minh
#include <winsock2.h>     // Winsock2 API — socket, bind, connect, send, recv, sendto, recvfrom...
#include <ws2tcpip.h>     // inet_pton, inet_ntop — chuyển đổi địa chỉ IP chuỗi <-> nhị phân
#include <windows.h>      // Windows API — HANDLE, GetStdHandle, Console API (SetConsoleCursorPosition...)
#include <cstdint>        // uint8_t, uint16_t, uint32_t — kiểu số nguyên kích thước cố định
#include <random>         // mt19937, random_device, uniform_int_distribution — sinh số ngẫu nhiên (dùng cho giả lập mất gói)
#include <iomanip>        // hex, setfill, setw — định dạng xuất số hex
#include <filesystem>     // std::filesystem — thao tác file/thư mục (tạo, xóa, kiểm tra...)
#include <system_error>   // std::error_code — mã lỗi hệ thống

// Liên kết thư viện Winsock2 (ws2_32.lib) tự động khi biên dịch
#pragma comment(lib, "ws2_32.lib")

// === Khai báo using để viết gọn tên các kiểu/hàm thường dùng ===
using std::cerr, std::cout, std::cin,
std::stoi, std::endl,
std::format, std::toupper,
std::string, std::ios,
std::vector, std::atomic,
std::stringstream,
std::ostringstream,
std::istringstream,
std::ifstream,
std::ofstream,
std::tm,
std::mutex,
std::lock_guard,
std::unique_lock,
std::condition_variable,
std::thread,
std::shared_ptr,
std::make_shared,
std::istreambuf_iterator,
std::hex,
std::setfill,
std::setw,
std::mt19937,
std::random_device,
std::uniform_int_distribution,
std::error_code;

// Alias cho namespace dài — giúp code ngắn gọn hơn
namespace chr = std::chrono;       // chr::steady_clock, chr::milliseconds...
namespace fs = std::filesystem;     // fs::path, fs::exists, fs::create_directories...

// === Hằng số toàn cục ===
constexpr unsigned short CONTROL_PORT = 8080;  // Cổng TCP mặc định của kênh điều khiển (Control Channel)
constexpr int CHUNK_SIZE = 1024;               // Kích thước chunk dữ liệu cơ bản (byte)

// Đường dẫn thư mục gốc của Client — tất cả file tải về/upload đều nằm trong thư mục này
// fs::current_path() trả về thư mục hiện tại khi chạy chương trình
inline const fs::path CLIENT_ROOT = fs::current_path() / "client_root";

// === Enum chế độ truyền dữ liệu (Data Connection Mode) ===
// Quyết định cách thiết lập kênh dữ liệu UDP giữa Client và Server
enum class DataMode {
	NONE,    // Chưa thiết lập chế độ nào — cần gửi PORT hoặc PASV trước khi truyền dữ liệu
	ACTIVE,  // Active mode — Client mở cổng, Server kết nối đến (lệnh PORT)
	PASSIVE  // Passive mode — Server mở cổng, Client kết nối đến (lệnh PASV)
};
