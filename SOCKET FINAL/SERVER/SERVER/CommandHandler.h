#pragma once
#include "lib.h"
#include "Session.h"

enum class FtpCmd {
	USER, PASS, QUIT, NOOP, PWD, CWD, CDUP, MKD,
	RMD, LIST, NLST, STAT, SIZE, MDTM, TYPE, MODE,
	PORT, PASV, RETR, STOR, STOU, APPE, DELE, RNFR, 
	RNTO, HASH, ABOR, HELP,

	UNKNOWN
};

FtpCmd toFtpCmd(const string&);

class CommandHandler {
private:
	//Giai đoạn 2
	string handleUser(Session&, const string&);
	string handlePass(Session&, const string&);
	string handlePwd(Session&);
	string handleNoop();
	string handleQuit();
	string handleHelp(const string&);

	//Giai đoạn 3
	string handleType(Session&, const string&);
	string handleMode(Session&, const string&);
	string handleSize(Session&, const string&);
	string handleMdtm(Session&, const string&);
	string handleStat(Session&, const string&);

	string handleStor(Session&, const string&);
	string handleRetr(Session&, const string&);
public:
	string handle(Session&, const string&, const string&);
};