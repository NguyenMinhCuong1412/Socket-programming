#pragma once
#include <iostream>
#include <format>
#include <vector>
#include <string>
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
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

using std::cerr, std::cout, std::stoi,
std::endl, std::format, std::toupper,
std::string, std::ios, std::vector,
std::stringstream,                      //String Stream: biến <-> chuỗi
std::ostringstream,                     //Output String Stream: biến -> ghi ra chuỗi
std::istringstream,                     //Input String Stream: đọc từ chuỗi -> biến
std::ifstream,                          //Input File Stream: đọc từ file -> biến
std::ofstream,                          //Output File Stream: biến -> ghi ra file
std::tm,                                //Cấu trúc lưu trữ các thành phần thời gian (time structure)
std::error_code,                        //Lưu trữ mã lỗi hệ thống (system error code)
std::mutex,                             //Ổ khóa nhị phân (0/1) đại diện cho tài nguyên chung, tránh truy cập đồng thời từ nhiều luồng
std::lock_guard,                        //Bảo vệ tài nguyên chung không bị lấn chiếm, phải xếp hàng chờ 
std::thread,                            //Tạo một Luồng chạy ngầm (Detached Thread) để xử lý kết nối từ một Client mới 
std::shared_ptr,                        //Smart Pointer: quản lý vòng đời của đối tượng (Object Lifetime) và cho phép nhiều con trỏ trỏ đến cùng một đối tượng trong bộ nhớ Heap, tránh rò rỉ bộ nhớ
std::make_shared,                       //Tạo một đối tượng trong 1 vùng bộ nhớ Heap và trả về một con trỏ thông minh (Smart Pointer) trỏ đến đối tượng đó, quản lý vòng đời của đối tượng
std::atomic;                            

namespace fs = std::filesystem;
namespace chr = std::chrono;

// Chỉ CONTROL_PORT cần cố định: đây là cổng "well-known" duy nhất mà Client PHẢI biết trước
// để mở kết nối TCP ban đầu (không có kênh nào khác để server báo trước cổng này).
// Cổng dữ liệu UDP cho STOR/APPE/STOU (trước là SERVER_DATA_PORT=8081) và cho RETR khi Client
// không dùng PORT/PASV (trước là CLIENT_DATA_PORT=8082) giờ đã chuyển sang NGẪU NHIÊN do OS cấp
// (bind port=0) và được thông báo qua kênh điều khiển (xem CmdHandler::appendPortIfNeeded và
// ControlChannel::autoNegotiateActivePort bên Client) thay vì cố định cứng trong code.
constexpr unsigned short CONTROL_PORT = 8080;     //Server bind cổng cố định để nhận lệnh FTP từ Client - 21
constexpr int CHUNK_SIZE = 1024;

//Thư mục làm việc của Client lúc Server khởi động (working directory của process)
inline const fs::path SERVER_ROOT = fs::current_path() / "server_root";

//Tránh truy cập của nhiều thread (nhiều client) in xen kẽ, lẫn lộn ra console
inline mutex g_coutMutex; //g_: biến toàn cục

//inline: tất cả các file dùng chung 1 biến này khi chương trình gộp các file để chạy

//Phục vụ cho PORT/PASV
enum class DataMode { 
    NONE, 
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