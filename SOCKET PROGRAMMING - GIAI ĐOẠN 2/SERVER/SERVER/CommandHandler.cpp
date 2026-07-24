#include "CommandHandler.h"

FtpCommand toFtpCommand(const string& command) {
    if (command == "USER") return FtpCommand::USER;
    if (command == "PASS") return FtpCommand::PASS;
    if (command == "QUIT") return FtpCommand::QUIT;
    if (command == "NOOP") return FtpCommand::NOOP;
    if (command == "PWD")  return FtpCommand::PWD;
    if (command == "CWD")  return FtpCommand::CWD;
    if (command == "CDUP") return FtpCommand::CDUP;
    if (command == "MKD")  return FtpCommand::MKD;
    if (command == "RMD")  return FtpCommand::RMD;
    if (command == "LIST") return FtpCommand::LIST;
    if (command == "NLST") return FtpCommand::NLST;
    if (command == "STAT") return FtpCommand::STAT;
    if (command == "SIZE") return FtpCommand::SIZE;
    if (command == "MDTM") return FtpCommand::MDTM;
    if (command == "TYPE") return FtpCommand::TYPE;
    if (command == "MODE") return FtpCommand::MODE;
    if (command == "PORT") return FtpCommand::PORT;
    if (command == "PASV") return FtpCommand::PASV;
    if (command == "RETR") return FtpCommand::RETR;
    if (command == "STOR") return FtpCommand::STOR;
    if (command == "STOU") return FtpCommand::STOU;
    if (command == "APPE") return FtpCommand::APPE;
    if (command == "DELE") return FtpCommand::DELE;
    if (command == "RNFR") return FtpCommand::RNFR;
    if (command == "RNTO") return FtpCommand::RNTO;
    if (command == "HASH") return FtpCommand::HASH;
    if (command == "ABOR") return FtpCommand::ABOR;
    if (command == "HELP") return FtpCommand::HELP;
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