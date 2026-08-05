#pragma once

#include <iostream>
#include <format>       
#include <vector>       
#include <string>
#include <map>          //Làm việc với bảng ánh xạ khóa-giá trị (dùng cho bảng session đang hoạt động)
#include <sstream>      //Làm việc với string và luồng chuỗi (string stream)
#include <fstream>      //Đọc/Ghi file
#include <cctype>       //Làm việc với với char
#include <ctime>        //Làm việc với thời gian - độ chính xác thấp
#include <system_error> //Làm việc với mã lỗi hệ thống
#include <filesystem>   //Làm việc với hệ thống file
#include <chrono>       //Làm việc với thời gian - độ chính xác cao
#include <mutex>        //Làm việc với Data Race (Xung đột dữ liệu) và Critical Section (Vùng tranh chấp) khi có nhiều luồng cùng truy cập vào một vùng nhớ
#include <thread>       //Làm việc với luồng thực thi/tác vụ song song (parallel) hoặc đồng thời (concurrent) - cấp độ hệ điều hành 
#include <atomic>       //Làm việc với các thao tác dữ liệu mức thấp (CPU assembly level) không thể bị ngắt quãng
#include <memory>       //Làm việc với bộ nhớ Dynamic (Heap memory) và vòng đời của đối tượng (Object Lifetime)
#include <winsock2.h>   //Windows Sockets 2: thư viện chính cung cấp các API nền tảng làm việc với Socket trên Windows 
#include <ws2tcpip.h>   //Winsock 2 TCP/IP: thư viện bổ sung cung cấp các công cụ nâng cao chuyên dụng cho giao thức TCP/IP
#include <windows.h>    //Nạp file header trung tâm của Windows SDK, cho phép chương trình C/C++ gọi và sử dụng các API hệ thống của Windows
#include <bcrypt.h>     //Nạp file header chứa khai báo cho Windows CNG (Cryptography Next Generation) API - bộ công cụ mã hóa chính chủ của Microsoft Windows
#include <cstdint>      //C Standard Integer Types: quản lý các kiểu dữ liệu số nguyên (integer) có kích thước cố định chính xác về số bit (Fixed-width integer types)
#include <random>       //Random Number Generation: tạo ra các số ngẫu nhiên chất lượng cao và phân bố số ngẫu nhiên theo các quy luật thống kê
#include <iomanip>      //Làm việc với định dạng xuất dữ liệu (setw, setfill, hex, ...)

#pragma comment(lib, "bcrypt.lib") //Liên kết thư viện mã hóa Windows CNG (Cryptography Next Generation)
#pragma comment(lib, "ws2_32.lib") //Liên kết thư viện Winsock 2 (Windows Sockets 2) API

using std::cerr, std::cout, std::cin,
std::stoi, std::endl,
std::format, std::toupper,
std::string, std::ios,
std::vector, std::atomic,
std::stringstream,            //String Stream: biến <-> chuỗi
std::ostringstream,           //Output String Stream: biến -> ghi ra chuỗi
std::istringstream,           //Input String Stream: đọc từ chuỗi -> biến
std::ifstream,                //Input File Stream: đọc từ file -> biến
std::ofstream,                //Output File Stream: biến -> ghi ra file
std::tm,                      //Cấu trúc lưu trữ các thành phần thời gian (time structure)
std::error_code,              //Lưu trữ mã lỗi hệ thống (system error code)
std::mutex,                   //Ổ khóa nhị phân (0/1) đại diện cho tài nguyên chung, tránh truy cập đồng thời từ nhiều luồng
std::lock_guard,              //Bảo vệ tài nguyên chung không bị lấn chiếm, phải xếp hàng chờ 
std::map,                     //Bảng ánh xạ khóa-giá trị có thứ tự (dùng lưu bảng session đang hoạt động)
std::thread,                  //Tạo một Luồng chạy ngầm (Detached Thread) để xử lý kết nối từ một Client mới 
std::shared_ptr,              //Smart Pointer: quản lý vòng đời của đối tượng (Object Lifetime) và cho phép nhiều con trỏ trỏ đến cùng một đối tượng trong bộ nhớ Heap, tránh rò rỉ bộ nhớ
std::make_shared,             //Tạo một đối tượng trong 1 vùng bộ nhớ Heap và trả về một con trỏ thông minh (Smart Pointer) trỏ đến đối tượng đó, quản lý vòng đời của đối tượng
std::istreambuf_iterator,     //Con trỏ đặc biệt dùng để đọc trực tiếp từng ký tự (hoặc byte) thô từ bộ đệm của một input stream
std::hex,                     //Chuyển định dạng xuất/nhập số nguyên sang hệ thập lục phân (Hexadecimal - Cơ số 16)
std::setfill,                 //Đặt ký tự lấp đầy (fill character) cho phần khoảng trống được tạo ra bởi std::setw
std::setw,                    //Đặt độ rộng tối thiểu (width) cho dữ liệu tiếp theo sẽ xuất ra luồng
std::mt19937,                 //Bộ sinh số giả ngẫu nhiên dựa trên thuật toán Mersenne Twister (chu kỳ cực dài 2^{19937}-1)
std::random_device,           //Lấy một chuỗi hạt giống (seed) ngẫu nhiên thực sự từ phần cứng máy tính
std::uniform_int_distribution;//Nắn các số ngẫu nhiên thô từ std::mt19937 sao cho chúng phân bố đồng đều (uniform distribution) trong một khoảng số nguyên [a, b] xác định

//namespace: tầng dữ liệu quá sâu, các hàm quá nguy hiểm tới hệ thống máy tính, nên phải chi đích danh hàm bằng bí danh
namespace fs = std::filesystem;
namespace chr = std::chrono;

//Constant Expression - Biểu thức hằng: kết quả luôn có sẵn để compiler lấy để tính toán, giảm thời gian chạy và CPU
constexpr unsigned short CONTROL_PORT = 8080; //Server-TCP bind cổng cố định để nhận lệnh FTP từ Client-TCP  
constexpr int CHUNK_SIZE = 1024;              //Kích thước tối đa của 1 gói tin TCP

//inline: tất cả các file dùng chung 1 biến này khi chương trình gộp các file để chạy
inline const fs::path SERVER_ROOT = fs::current_path() / "server_root"; //Thư mục làm việc của Client lúc Server khởi động (working directory của process)
inline mutex g_coutMutex; //Tránh truy cập của nhiều thread (nhiều client) in xen kẽ, lẫn lộn ra console - g_: biến toàn cục 

//Phục vụ cho PORT/PASV
enum class DataMode { 
    NONE,   //Chưa thiết lập chế độ -> báo lỗi
	ACTIVE, //Client tự chọn port -> báo cho Server -> Server chủ động kết nối sang Client để truyền/nhận dữ liệu
	PASSIVE //Server tự chọn port -> báo cho Client -> Client chủ động kết nối sang Server để truyền/nhận dữ liệu
};

//Giá trị cố định của các mã lệnh FTP
enum class FtpCommand {
    USER, PASS, QUIT, NOOP, PWD,
    CWD, CDUP, MKD, RMD, LIST,
    NLST, STAT, SIZE, MDTM, TYPE,
    MODE, PORT, PASV, RETR, STOR,
    STOU, APPE, DELE, RNFR, RNTO,
    HASH, ABOR, HELP,
    UNKNOWN
};
