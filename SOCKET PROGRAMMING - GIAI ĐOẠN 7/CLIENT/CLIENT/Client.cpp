// ======================================================================
// Client.cpp — ĐIỂM KHỞI CHẠY (ENTRY POINT) CỦA CHƯƠNG TRÌNH FTP CLIENT
//    Khởi tạo Winsock, tạo thư mục gốc Client, khởi chạy kênh điều khiển
//    TCP kết nối đến Server, rồi chạy vòng lặp nhập lệnh FTP từ người dùng.
// ======================================================================
#include "lib.h"
#include "ControlChannel.h"

int main(int argc, char* argv[]) {
    // Khởi tạo Winsock 2.2 — bắt buộc trước khi sử dụng bất kỳ hàm socket nào trên Windows
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << "421 Service not available, WSAStartup failed" << endl;  // 421: dịch vụ không khả dụng
        return 1;
    }

    // Tạo thư mục gốc của Client (client_root) — nơi lưu trữ file tải về/upload
    error_code ec;
    fs::create_directories(CLIENT_ROOT, ec);
    if (ec) {
        cerr << format("421 Service not available, cannot create client root '{}': {}", CLIENT_ROOT.string(), ec.message()) << endl;
        WSACleanup();
        return 1;
    }
    cout << "Client root: " << CLIENT_ROOT.string() << endl;

    // Khởi tạo kênh điều khiển TCP, kết nối đến Server tại 127.0.0.1:CONTROL_PORT
    ControlChannel control(CONTROL_PORT, "127.0.0.1");
    if (!control.start()) {
        WSACleanup();
        return 1;
    }
    // Chạy vòng lặp chính: nhập lệnh → gửi → nhận phản hồi
    control.run();
    // Đóng kết nối
    control.stop();

    // Giải phóng tài nguyên Winsock
    WSACleanup();
    return 0;
}
