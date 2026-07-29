#pragma once
#include "lib.h"
#include "Session.h"

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

//Xử lý lệnh FTP
void parseCmd(const string&, string&, string&);

//Chuyển đổi lệnh: string -> enum
FtpCommand toFtpCommand(const string& command);

//Hàm lệnh FTP
class CommandHandler {
private:
    SOCKET clientSocket = INVALID_SOCKET; //socket của Client được accept
    string clientIp; //IP của Client kết nối tới

    //Gửi xác nhận qua trung gian
    void sendIntermediate(const string& msg);

    //Giải quyết vấn đề đường dẫn thật trên ổ đĩa
    fs::path resolvePath(const Session& s, const string& arg, string& outLogical);

    //Người dùng
    string handleUser(Session&, const string&);  

    //Mật khẩu
    string handlePass(Session&, const string&); 

    //Print Working Directory - Hiển thị thư mục làm việc
    string handlePwd(Session&);   

    //No Operation - Không thao tác
    string handleNoop();   

    //Thoát
    string handleQuit(); 

    //Tra cứu
    string handleHelp(const string&);

    //Kiểu dữ liệu
    string handleType(Session&, const string&);

    //Chế độ truyền tải
    string handleMode(Session&, const string&);

    //Kích thước
    string handleSize(Session&, const string&);

    //Status - Trạng thái
    string handleStat(Session&, const string&);

    //Modification Time - Thời gian điều chỉnh
    string handleMdtm(Session&, const string&);

    //Store = Upload - Tải lên
    string handleStor(Session&, const string&);

    //Retrieve = Download - Tải xuống
    string handleRetr(Session&, const string&);

    //Change Working Directory - Thay đổi thư mục làm việc
    string handleCwd(Session&, const string&);

    //Change to Parent Directory - Di chuyển lên thư mục cha của thư mục hiện tại
    string handleCdup(Session&);

    //Make Directory - Tạo thư mục mới
    string handleMkd(Session&, const string&);

    //Remove Directory - Xóa thư mục trống 
    string handleRmd(Session&, const string&);

    //Liệt kê toàn bộ danh sách tệp/thư mục ở vị trí hiện tại + thông tin chi tiết
    string handleList(Session&);

    //Liệt kê toàn bộ danh sách tệp/thư mục ở vị trí hiện tại
    string handleNlst(Session&);

    //Store Unique = STOR + Server tự đặt 1 tên độc nhất 
    string handleStou(Session&);

    //Append - Tải lên và gắn nối vào cuối 1 tệp đã tồn tại
    string handleAppe(Session&, const string&);

    //Delete - Xóa 1 tệp 
    string handleDele(Session&, const string&);

    //Rename From - Chỉ định tệp cần đổi tên
    string handleRnfr(Session&, const string&);

    //Rename To - Đặt tên mới
    string handleRnto(Session&, const string&);
public:
    void setControlSocket(SOCKET s) { this->clientSocket = s; }
    void setClientIp(const string& ip) { this->clientIp = ip; }

    //Điều phối các lệnh FTP
    string handle(Session&, const string&, const string&);
};