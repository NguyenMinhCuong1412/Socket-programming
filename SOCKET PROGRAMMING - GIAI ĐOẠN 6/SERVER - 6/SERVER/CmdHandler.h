#pragma once
#include "lib.h"
#include "Session.h"

//Xử lý lệnh FTP
void parseCmd(const string&, string&, string&);

//Chuyển đổi lệnh: string -> enum
FtpCommand toFtpCommand(const string& command);

//Hàm lệnh FTP
class CommandHandler {
private:
    SOCKET clientSocket;   //socket của Client được accept
    string clientIp;       //IP của Client kết nối tới
    thread transferThread; //luồng phụ của Client chỉ dùng cho truyền/nhận file
    
    void joinPreviousTransfer();              //Kiểm tra luồng phụ 
    void sendIntermediate(const string& msg); //Gửi xác nhận qua trung gian
    fs::path resolvePath(const Session& s, const string& arg, string& outLogical);  //Giải quyết vấn đề đường dẫn thật trên ổ đĩa

    string handleUser(Session&, const string&); //Người dùng
    string handlePass(Session&, const string&); //Mật khẩu
    string handlePwd(Session&);                 //Print Working Directory - Hiển thị thư mục làm việc
    string handleNoop();                        //No Operation - Không thao tác 
    string handleQuit();                        //Thoát
    string handleHelp(const string&);           //Tra cứu
    string handleType(Session&, const string&); //Kiểu dữ liệu
    string handleMode(Session&, const string&); //Chế độ truyền tải
    string handleSize(Session&, const string&); //Kích thước
    string handleStat(Session&, const string&); //Status - Trạng thái
    string handleMdtm(Session&, const string&); //Modification Time - Thời gian điều chỉnh
    string handleStor(Session&, const string&); //Store = Upload - Tải lên
    string handleRetr(Session&, const string&); //Retrieve = Download - Tải xuống
    string handleCwd(Session&, const string&);  //Change Working Directory - Thay đổi thư mục làm việc
    string handleCdup(Session&);                //Change to Parent Directory - Di chuyển lên thư mục cha của thư mục hiện tại
    string handleMkd(Session&, const string&);  //Make Directory - Tạo thư mục mới
    string handleRmd(Session&, const string&);  //Remove Directory - Xóa thư mục trống 
    string handleList(Session&);                //Liệt kê toàn bộ danh sách tệp/thư mục ở vị trí hiện tại + thông tin chi tiết
    string handleNlst(Session&);                //Liệt kê toàn bộ danh sách tệp/thư mục ở vị trí hiện tại
    string handleStou(Session&);                //Store Unique = STOR + Server tự đặt 1 tên độc nhất 
    string handleAppe(Session&, const string&); //Append - Tải lên và gắn nối vào cuối 1 tệp đã tồn tại
    string handleDele(Session&, const string&); //Delete - Xóa 1 tệp 
    string handleRnfr(Session&, const string&); //Rename From - Chỉ định tệp cần đổi tên
    string handleRnto(Session&, const string&); //Rename To - Đặt tên mới
    string handleHash(Session&, const string&); //Hash - Tính SHA-256 hash của 1 file trên server
    string handlePort(Session&, const string&); //Client báo địa chỉ IP:port của mình (chế độ ACTIVE)
    string handlePasv(Session&);                //Server tự chọn 1 port, mở sẵn, chờ client kết nối tới (chế độ PASSIVE)
    string handleAbor(Session&);                //Hủy transfer đang chạy
    unsigned short pickListenPort(Session&);    //Chọn port server LẮNG NGHE cho STOR/APPE/STOU dựa theo Session::dataMode
public:
    CommandHandler();           
    ~CommandHandler(); //join transferThread nếu còn đang chạy

    void setControlSocket(SOCKET s) { this->clientSocket = s; } //Lưu trữ socket của Client được accept
    void setClientIp(const string& ip) { this->clientIp = ip; } //Lưu trữ IP của Client kết nối tới
    
    string handle(Session&, const string&, const string&); //Điều phối các lệnh FTP
};