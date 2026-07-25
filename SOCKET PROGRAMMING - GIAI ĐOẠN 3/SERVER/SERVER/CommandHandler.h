#pragma once
#include "lib.h"
#include "Session.h"
#include "DataChannel.h"

enum class FtpCommand {
    USER, PASS, QUIT, NOOP, PWD, CWD, CDUP, MKD, RMD, LIST, NLST, STAT,
    SIZE, MDTM, TYPE, MODE, PORT, PASV, RETR, STOR, STOU, APPE, DELE,
    RNFR, RNTO, HASH, ABOR, HELP,
    UNKNOWN
};

FtpCommand toFtpCommand(const string& command);

class CommandHandler {
private:
    string clientIp;
    SOCKET controlSocket; // dùng để gửi phản hồi trung gian (150) trước khi block ở UDP

    void sendIntermediate(const string& msg); // gửi "150 ..." qua control channel

    string handleUser(Session&, const string&);
    string handlePass(Session&, const string&);
    string handlePwd(Session&);
    string handleNoop();
    string handleQuit();
    string handleHelp(const string&);
    string handleType(Session&, const string&);
    string handleMode(Session&, const string&);
    string handleSize(const string&);
    string handleMdtm(const string&);
    string handleStat(const string&);
    string handleStor(Session&, const string&);
    string handleRetr(Session&, const string&);
public:
    CommandHandler(const string& clientIp, SOCKET controlSocket);
    string handle(Session&, const string&, const string&);
};