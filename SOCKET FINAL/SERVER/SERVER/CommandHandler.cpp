#include "CommandHandler.h"

FtpCmd toFtpCmd(const string& cmd) {
	if (cmd == "USER") return FtpCmd::USER;
	if (cmd == "PASS") return FtpCmd::PASS;
	if (cmd == "QUIT") return FtpCmd::QUIT;
	if (cmd == "NOOP") return FtpCmd::NOOP;
	if (cmd == "PWD") return FtpCmd::PWD;
	if (cmd == "CWD") return FtpCmd::CWD;
	if (cmd == "CDUP") return FtpCmd::CDUP;
	if (cmd == "MKD") return FtpCmd::MKD;
	if (cmd == "RMD") return FtpCmd::RMD;
	if (cmd == "LIST") return FtpCmd::LIST;
	if (cmd == "NLST") return FtpCmd::NLST;
	if (cmd == "STAT") return FtpCmd::STAT;
	if (cmd == "SIZE") return FtpCmd::SIZE;
	if (cmd == "MDTM") return FtpCmd::MDTM;
	if (cmd == "TYPE") return FtpCmd::TYPE;
	if (cmd == "MODE") return FtpCmd::MODE;
	if (cmd == "PORT") return FtpCmd::PORT;
	if (cmd == "PASV") return FtpCmd::PASV;
	if (cmd == "RETR") return FtpCmd::RETR;
	if (cmd == "STOR") return FtpCmd::STOR;
	if (cmd == "STOU") return FtpCmd::STOU;
	if (cmd == "APPE") return FtpCmd::APPE;
	if (cmd == "DELE") return FtpCmd::DELE;
	if (cmd == "RNFR") return FtpCmd::RNFR;
	if (cmd == "RNTO") return FtpCmd::RNTO;
	if (cmd == "HASH") return FtpCmd::HASH;
	if (cmd == "ABOR") return FtpCmd::ABOR;
	if (cmd == "HELP") return FtpCmd::HELP;
	return FtpCmd::UNKNOWN;
}

string CommandHandler::handleUser(Session& s, const string& arg) {
	if (arg.empty()) return "501 Syntax error in parameters\r\n";
	s.setName(arg);
	return "331 Username OK, need password\r\n";
}

string CommandHandler::handlePass(Session& s, const string& arg) {
	if (s.getName().empty()) return "503 Bad sequence of commands\r\n";
	if (arg.empty()) return "501 Syntax error in parameters\r\n";
	s.setLogged(true);
	return "230 Login successful\r\n";
}

string CommandHandler::handlePwd(Session& s) {
	if (!s.getLogged()) return "530 Not logged in\r\n";
	return format("257 \"{}\" is current directory\r\n", s.getDir());
}

string CommandHandler::handleNoop() { return "200 NOOP OK\r\n"; }

string CommandHandler::handleQuit() { return "221 Goodbye\r\n"; }

string CommandHandler::handleHelp(const string& arg) {
	if (arg.empty()) {
		string response = "214 The following commands are recognized:\r\n";
		response += "        USER  PASS  QUIT  NOOP  PWD  CWD  CDUP  MKD\n";
		response += "        RMD  LIST  NLST  STAT  SIZE  MDTM  TYPE  MODE\n";
		response += "        PORT  PASV  RETR  STOR  STOU  APPE  DELE  RNFR\n";
		response += "        RNTO  HASH  ABOR  HELP\n";
		response += "        214 Syntax: HELP [COMMAND] - Show help for a specific one\r\n";
		return response;
	}

	string cmd = arg;
	for (auto& c : cmd) c = toupper(c);

	switch (toFtpCmd(cmd)) {
	case FtpCmd::USER: return "214 Syntax: USER <username> - Send username to start authentication\r\n";
	case FtpCmd::PASS: return "214 Syntax: PASS <password> - Send password to complete authentication\r\n";
	case FtpCmd::PWD:  return "214 Syntax: PWD - Print current working directory\r\n";
	case FtpCmd::NOOP: return "214 Syntax: NOOP - No operation, keep-alive ping\r\n";
	case FtpCmd::QUIT: return "214 Syntax: QUIT - Terminate the control connection\r\n";
	default: return format("501 Syntax error in parameters, unknown command '{}'\r\n", cmd);
	}
}

string CommandHandler::handle(Session& s, const string& cmd, const string& arg) {
	switch (toFtpCmd(cmd)) {
	case FtpCmd::USER:return handleUser(s, arg);
	case FtpCmd::PASS:return handlePass(s, arg);
	case FtpCmd::PWD:return handlePwd(s);
	case FtpCmd::NOOP:return handleNoop(); 
	case FtpCmd::QUIT:return handleQuit();
	case FtpCmd::HELP:return handleHelp(arg);
	default: return "502 Command not implemented\r\n";
	}
}