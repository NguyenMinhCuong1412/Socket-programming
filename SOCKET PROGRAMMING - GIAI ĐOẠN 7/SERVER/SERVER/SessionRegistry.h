// ======================================================================
// SessionRegistry.h — BẢNG ĐĂNG KÝ PHIÊN LÀM VIỆC (SESSION REGISTRY)
//    Quản lý danh sách tất cả các phiên Client đang kết nối đến Server.
//    Lưu thông tin: IP, username, trạng thái đăng nhập, thư mục hiện tại,
//    lệnh cuối cùng, thời điểm kết nối. Hỗ trợ hiển thị bảng trạng thái
//    trên console Server (lệnh admin "sessions"/"who").
//    Thread-safe: tất cả thao tác đều được bảo vệ bằng mutex.
// ======================================================================
#pragma once
#include "lib.h"

// Bản ghi thông tin của một Client đang kết nối
struct ClientRecord {
    string ip;                              // Địa chỉ IP của Client
    string userName;                        // Tên người dùng (sau lệnh USER)
    bool   loggedIn;                        // Đã đăng nhập thành công chưa (sau lệnh PASS)
    string currentDir;                      // Thư mục làm việc hiện tại
    string lastCommand;                     // Lệnh FTP cuối cùng Client gửi
    chr::system_clock::time_point connectedAt; // Thời điểm Client kết nối
};

// Singleton-like class (sử dụng static member) quản lý bảng phiên
// Mỗi phiên được đánh dấu bằng SOCKET descriptor của kết nối TCP
class SessionRegistry {
public:
    // Thêm Client mới vào bảng khi accept() kết nối TCP
    static void add(SOCKET sock, const string& ip);
    // Xóa Client khỏi bảng khi ngắt kết nối
    static void remove(SOCKET sock);
    // Cập nhật thông tin phiên (sau mỗi lệnh FTP)
    static void update(SOCKET sock, const string& userName, bool loggedIn,
        const string& currentDir, const string& lastCommand);

    // In bảng trạng thái ra console Server (admin command)
    static void printTable();
    // Trả về số phiên đang hoạt động
    static size_t count();

private:
    static mutex mtx;                      // Mutex bảo vệ bảng phiên — tránh race condition khi nhiều thread client cùng truy cập
    static map<SOCKET, ClientRecord> table; // Bảng ánh xạ: SOCKET → ClientRecord
};
