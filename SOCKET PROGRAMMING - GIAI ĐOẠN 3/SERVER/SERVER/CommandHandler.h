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
    string clientIp; // dùng để server gửi RETR ngược lại đúng client

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
    CommandHandler(const string& clientIp);
    string handle(Session&, const string&, const string&);
};