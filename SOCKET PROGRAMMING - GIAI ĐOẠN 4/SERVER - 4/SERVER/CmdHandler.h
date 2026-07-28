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
    SOCKET controlSocket = INVALID_SOCKET;
    string clientIp;

    //Gửi xác nhận qua trung gian
    void sendIntermediate(const string& msg);

    //Giải quyết vấn đề đường dẫn thật trên ổ đĩa
    fs::path resolvePath(const Session& s, const string& arg, string& outLogical);

    //Hàm lệnh FTP cụ thể
    string handleUser(Session&, const string&);
    string handlePass(Session&, const string&);
    string handlePwd(Session&);
    string handleNoop();
    string handleQuit();
    string handleHelp(const string&);

    string handleType(Session&, const string&);
    string handleMode(Session&, const string&);
    string handleSize(Session&, const string&);
    string handleStat(Session&, const string&);
    string handleMdtm(Session&, const string&);
    string handleStor(Session&, const string&);
    string handleRetr(Session&, const string&);

    string handleCwd(Session&, const string&);
    string handleCdup(Session&);
    string handleMkd(Session&, const string&);
    string handleRmd(Session&, const string&);
    string handleList(Session&);
    string handleNlst(Session&);
    string handleStou(Session&);
    string handleAppe(Session&, const string&);
    string handleDele(Session&, const string&);
    string handleRnfr(Session&, const string&);
    string handleRnto(Session&, const string&);
public:
    void setControlSocket(SOCKET s) { this->controlSocket = s; }
    void setClientIp(const string& ip) { this->clientIp = ip; }

    //Điều phối các lệnh FTP
    string handle(Session&, const string&, const string&);
};