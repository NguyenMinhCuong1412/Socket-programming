#pragma once
#include "lib.h"

//Thông tin snapshot của 1 Client-TCP đang kết nối, dùng để hiển thị "active session table"
//(mục 4.5.3 đề bài: "Server log displays connected client IPs, executed commands, and active session table")
struct ClientRecord {
    string ip;              //IP của Client-TCP
    string userName;        //Tên đăng nhập (rỗng nếu chưa USER)
    bool   loggedIn;        //Đã hoàn tất USER+PASS chưa
    string currentDir;      //Thư mục ảo hiện tại trên Server
    string lastCommand;     //Lệnh gần nhất Client vừa gửi
    chr::system_clock::time_point connectedAt; //Thời điểm kết nối
};

//SessionRegistry — bảng TOÀN CỤC (static) theo dõi mọi session TCP đang sống
//Thread-safe: mỗi Client-TCP chạy trên 1 thread riêng (xem ControlChannel::handleClient),
//nên mọi thao tác đọc/ghi bảng đều phải qua mutex nội bộ.
class SessionRegistry {
public:
    static void add(SOCKET sock, const string& ip);      //Gọi khi 1 Client-TCP vừa được accept
    static void remove(SOCKET sock);                     //Gọi khi Client-TCP ngắt kết nối (QUIT/đóng socket)
    static void update(SOCKET sock, const string& userName, bool loggedIn,
        const string& currentDir, const string& lastCommand); //Gọi sau mỗi lệnh xử lý xong

    static void printTable();          //In bảng session hiện tại ra console Server (dùng cho log/demo)
    static size_t count();             //Số session đang hoạt động (phục vụ test đa client đồng thời)

private:
    static mutex mtx;                       //Bảo vệ table khỏi truy cập đồng thời từ nhiều thread Client
    static map<SOCKET, ClientRecord> table; //Khóa = socket của từng Client-TCP, đảm bảo duy nhất trong toàn Server
};
