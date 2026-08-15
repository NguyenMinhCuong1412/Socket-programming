// ======================================================================
// lib.h — FILE TIÊU ĐỀ CHUNG (COMMON HEADER) CỦA FTP SERVER
//    Tập trung toàn bộ các #include thư viện chuẩn C++, thư viện
//    Winsock2, Windows API (bao gồm BCrypt cho hàm băm SHA-256),
//    và các khai báo dùng chung (hằng số, enum, đường dẫn gốc) cho
//    toàn bộ chương trình Server. Mọi file .h/.cpp khác đều include file này.
// ======================================================================
#pragma once

// === Thư viện chuẩn C++ ===
#include <iostream>       // cout, cerr, cin — nhập xuất console
#include <format>         // std::format — định dạng chuỗi kiểu Python (C++20)
#include <vector>         // std::vector — mảng động
#include <string>         // std::string — chuỗi ký tự
#include <map>            // std::map — bảng ánh xạ (dùng cho SessionRegistry)
#include <sstream>        // stringstream — xử lý chuỗi dạng luồng
#include <fstream>        // ifstream, ofstream — đọc/ghi file
#include <cctype>         // toupper, isdigit — xử lý ký tự
#include <ctime>          // std::tm, localtime_s — thao tác thời gian
#include <system_error>   // std::error_code — mã lỗi hệ thống
#include <filesystem>     // std::filesystem — thao tác file/thư mục
#include <chrono>         // chr::steady_clock, chr::system_clock — đo thời gian
#include <mutex>          // mutex, lock_guard — đồng bộ hóa đa luồng
#include <thread>         // std::thread — tạo và quản lý luồng
#include <atomic>         // std::atomic — biến nguyên tử, an toàn đa luồng
#include <memory>         // shared_ptr, make_shared — quản lý bộ nhớ thông minh
#include <winsock2.h>     // Winsock2 API — socket, bind, listen, accept, send, recv...
#include <ws2tcpip.h>     // inet_pton, inet_ntop — chuyển đổi địa chỉ IP chuỗi <-> nhị phân
#include <windows.h>      // Windows API — HANDLE, NTSTATUS...
#include <bcrypt.h>       // BCrypt API — hàm băm SHA-256 (dùng cho lệnh HASH)
#include <cstdint>        // uint8_t, uint16_t, uint32_t — kiểu số nguyên kích thước cố định
#include <random>         // mt19937, random_device — sinh số ngẫu nhiên (giả lập mất gói)
#include <iomanip>        // hex, setfill, setw — định dạng xuất số hex

// Liên kết thư viện BCrypt và Winsock2 tự động khi biên dịch
#pragma comment(lib, "bcrypt.lib")
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
std::error_code,
std::mutex,
std::lock_guard,
std::map,
std::thread,
std::shared_ptr,
std::make_shared,
std::istreambuf_iterator,
std::hex,
std::setfill,
std::setw,
std::mt19937,
std::random_device,
std::uniform_int_distribution;

// Alias cho namespace dài
namespace fs = std::filesystem;     // fs::path, fs::exists, fs::file_size...
namespace chr = std::chrono;        // chr::system_clock, chr::milliseconds...

// === Hằng số toàn cục ===
constexpr unsigned short CONTROL_PORT = 8080;  // Cổng TCP mặc định của kênh điều khiển
constexpr int CHUNK_SIZE = 1024;               // Kích thước chunk dữ liệu cơ bản (byte)

// Đường dẫn thư mục gốc Server — tất cả file lưu trữ FTP nằm trong đây
inline const fs::path SERVER_ROOT = fs::current_path() / "server_root";
// Mutex bảo vệ cout — tránh output từ nhiều thread client xen lẫn nhau trên console Server
inline mutex g_coutMutex;

// === Enum chế độ truyền dữ liệu (Data Connection Mode) ===
enum class DataMode {
    NONE,    // Chưa thiết lập (Server mặc định bắt đầu ở ACTIVE nhưng client chưa gửi PORT)
	ACTIVE,  // Active mode — Server kết nối đến port Client đã chỉ định (lệnh PORT)
	PASSIVE  // Passive mode — Server mở port, Client kết nối đến (lệnh PASV)
};

// === Enum liệt kê tất cả lệnh FTP được Server hỗ trợ ===
// Dùng trong CmdHandler để dispatch lệnh vào hàm xử lý tương ứng
enum class FtpCommand {
    USER, PASS, QUIT, NOOP, PWD,     // Xác thực và quản lý phiên
    CWD, CDUP, MKD, RMD, LIST,      // Điều hướng và quản lý thư mục
    NLST, STAT, SIZE, MDTM, TYPE,   // Thông tin file/thư mục, chế độ truyền
    MODE, PORT, PASV, RETR, STOR,    // Chế độ dữ liệu, tải xuống/upload
    STOU, APPE, DELE, RNFR, RNTO,   // Upload đặc biệt, xóa, đổi tên
    HASH, ABOR, HELP,               // Tiện ích: băm file, hủy transfer, trợ giúp
    UNKNOWN                           // Lệnh không nhận diện được
};
