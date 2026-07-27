#include "CmdHandler.h"

void parseCmd(const string& raw, string& cmd, string& arg) {
	string clean = raw;
	while (!clean.empty() && (clean.back() == '\r' || clean.back() == '\n')) clean.pop_back();

	istringstream iss(clean);
	iss >> cmd;
	getline(iss, arg);
	if (!arg.empty() && arg[0] == ' ') arg = arg.substr(1);
	for (auto& c : cmd) c = toupper(c);
}

FtpCommand toFtpCommand(const string& cmd) {
    if (cmd == "USER") return FtpCommand::USER;
    if (cmd == "PASS") return FtpCommand::PASS;
    if (cmd == "QUIT") return FtpCommand::QUIT;
    if (cmd == "NOOP") return FtpCommand::NOOP;
    if (cmd == "PWD")  return FtpCommand::PWD;
    if (cmd == "CWD")  return FtpCommand::CWD;
    if (cmd == "CDUP") return FtpCommand::CDUP;
    if (cmd == "MKD")  return FtpCommand::MKD;
    if (cmd == "RMD")  return FtpCommand::RMD;
    if (cmd == "LIST") return FtpCommand::LIST;
    if (cmd == "NLST") return FtpCommand::NLST;
    if (cmd == "STAT") return FtpCommand::STAT;
    if (cmd == "SIZE") return FtpCommand::SIZE;
    if (cmd == "MDTM") return FtpCommand::MDTM;
    if (cmd == "TYPE") return FtpCommand::TYPE;
    if (cmd == "MODE") return FtpCommand::MODE;
    if (cmd == "PORT") return FtpCommand::PORT;
    if (cmd == "PASV") return FtpCommand::PASV;
    if (cmd == "RETR") return FtpCommand::RETR;
    if (cmd == "STOR") return FtpCommand::STOR;
    if (cmd == "STOU") return FtpCommand::STOU;
    if (cmd == "APPE") return FtpCommand::APPE;
    if (cmd == "DELE") return FtpCommand::DELE;
    if (cmd == "RNFR") return FtpCommand::RNFR;
    if (cmd == "RNTO") return FtpCommand::RNTO;
    if (cmd == "HASH") return FtpCommand::HASH;
    if (cmd == "ABOR") return FtpCommand::ABOR;
    if (cmd == "HELP") return FtpCommand::HELP;
    return FtpCommand::UNKNOWN;
}

string CommandHandler::handleUser(Session& s, const string& arg) {
    if (arg.empty()) return "501 Syntax error in parameters\r\n";
    s.setUserName(arg);
    return "331 Username OK, need password\r\n";
}

string CommandHandler::handlePass(Session& s, const string& arg) {
    if (s.getUserName().empty()) return "503 Bad sequence of commands\r\n";
    if (arg.empty()) return "501 Syntax error in parameters\r\n";
    s.setLoggedIn(true);
    return "230 Login successful\r\n";
}

string CommandHandler::handlePwd(Session& s) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";
    return format("257 \"{}\" is current directory\r\n", s.getDir());
}

string CommandHandler::handleNoop() { return "200 NOOP OK\r\n"; }

string CommandHandler::handleQuit() { return "221 Goodbye\r\n"; }

string CommandHandler::handleHelp(const string& arg) {
    if (arg.empty()) {
        string response = "214 The following commands are recognized:\r\n";
        response += "    USER    PASS    PWD     NOOP    QUIT    HELP\r\n";
        response += "214 Direct queries to this command using 'HELP <command>'.\r\n";
        return response;
    }

    string cmd = arg;
    for (auto& c : cmd) c = toupper(c);

    switch (toFtpCommand(cmd)) {
    case FtpCommand::USER: return "214 Syntax: USER <username> - Send username to start authentication\r\n";
    case FtpCommand::PASS: return "214 Syntax: PASS <password> - Send password to complete authentication\r\n";
    case FtpCommand::PWD:  return "214 Syntax: PWD - Print current working directory\r\n";
    case FtpCommand::NOOP: return "214 Syntax: NOOP - No operation, keep-alive ping\r\n";
    case FtpCommand::QUIT: return "214 Syntax: QUIT - Terminate the control connection\r\n";
    case FtpCommand::HELP: return "214 Syntax: HELP [command] - Show help for all commands or a specific one\r\n";
    default: return format("501 Syntax error in parameters, unknown command '{}'\r\n", cmd);
    }
}

string CommandHandler::handle(Session& s, const string& com, const string& arg) {
    switch (toFtpCommand(com)) {
    case FtpCommand::USER: return handleUser(s, arg);
    case FtpCommand::PASS: return handlePass(s, arg);
    case FtpCommand::PWD:  return handlePwd(s);
    case FtpCommand::NOOP: return handleNoop();
    case FtpCommand::QUIT: return handleQuit();
    case FtpCommand::HELP: return handleHelp(arg);
    default: return "502 Command not implemented\r\n";
    }
}
