#include "CmdHandler.h"
#include "DataChannel.h"

//Xử lý - tách lệnh FTP và đối số
void parseCmd(const string& raw, string& cmd, string& arg) {
    string clean = raw;
    while (!clean.empty() && (clean.back() == '\r' || clean.back() == '\n')) clean.pop_back();

    istringstream iss(clean);
    iss >> cmd;
    getline(iss, arg);
    if (!arg.empty() && arg[0] == ' ') arg = arg.substr(1);
    for (auto& c : cmd) c = toupper(c);
}

//Chuyển đổi lệnh từ string sang enum
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

void CommandHandler::sendIntermediate(const string& msg) { send(controlSocket, msg.c_str(), (int)msg.size(), 0); }

//Giai đoạn 1
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
        response += "    USER    PASS    PWD     NOOP    QUIT    HELP\n";
        response += "    TYPE    MODE    SIZE    STAT    MDTM    STOR\n";
        response += "    RETR\n";
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
    case FtpCommand::TYPE: return "214 Syntax: TYPE {A|I} - Set the data transfer type\r\n";
    case FtpCommand::MODE: return "214 Syntax: MODE {S|B|C} - Set the transfer mode\r\n";
    case FtpCommand::SIZE: return "214 Syntax: SIZE <filename> - Show the exact byte size of the specified file on the server\r\n";
    case FtpCommand::STAT: return "214 Syntax: STAT [path] - Show server status or, if a path is given, file/directory metadata\r\n";
    case FtpCommand::MDTM: return "214 Syntax: MDTM <filename> - Show the last modification timestamp of the specified file\r\n";
    case FtpCommand::STOR: return "214 Syntax: STOR <filename> - Upload a file from the client to the server\r\n";
    case FtpCommand::RETR: return "214 Syntax: RETR <filename> - Download the specified file from the server to the client\r\n";
    default: return format("501 Syntax error in parameters, unknown command '{}'\r\n", cmd);
    }
}

//Giai đoạn 2
string CommandHandler::handleType(Session& s, const string& arg) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";
    string t = arg;
    for (auto& c : t) c = toupper(c);
    if (t != "A" && t != "I") return "501 Syntax error in parameters\r\n";
    s.setType(t);
    return format("200 Set type to {}\r\n", t);
}

string CommandHandler::handleMode(Session& s, const string& arg) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";
    string m = arg;
    for (auto& c : m) c = toupper(c);
    if (m != "S" && m != "B" && m != "C") return "501 Syntax error in parameters\r\n";
    s.setMode(m);
    return format("200 Set mode to {}\r\n", m);
}

string CommandHandler::handleSize(Session& s, const string& arg) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";
    if (arg.empty()) return "501 Syntax error in parameters\r\n";

    fs::path filePath = fs::absolute(fs::path(s.getDir()) / arg);
    if (!fs::exists(filePath) || !fs::is_regular_file(filePath))
        return format("550 File unavailable, {} not found\r\n", filePath.string());
    return format("213 {}\r\n", fs::file_size(filePath));
}

string CommandHandler::handleMdtm(Session& s, const string& arg) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";
    if (arg.empty()) return "501 Syntax error in parameters\r\n";

    fs::path filePath = fs::absolute(fs::path(s.getDir()) / arg);
    if (!fs::exists(filePath))
        return format("550 File unavailable, {} not found\r\n", filePath.string());

    auto ftime = fs::last_write_time(filePath);
    auto sctp = chr::clock_cast<chr::system_clock>(ftime);
    auto tt = chr::system_clock::to_time_t(sctp);

    std::tm tm;
    localtime_s(&tm, &tt);

    return format("213 {:04}{:02}{:02}{:02}{:02}{:02}\r\n",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
}

string CommandHandler::handleStat(Session& s, const string& arg) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";
    if (arg.empty()) {
        return "211 FTP server status:\r\n"
            " User: " + s.getUserName() + "\r\n"
            " Current working directory: " + s.getDir() + "\r\n"
            " Data type: " + s.getType() + "\r\n"
            " Transfer mode: " + s.getMode() + "\r\n"
            "211 End of status\r\n";
    }
    else {
        fs::path filePath = fs::absolute(fs::path(s.getDir()) / arg);
        if (!fs::exists(filePath))
            return format("550 File unavailable, {} not found\r\n", filePath.string());
        if (fs::is_directory(filePath))
            return format("211 '{}' is a directory\r\n", filePath.string());
        else return format("211 Size of {}: {}\r\n", filePath.string(), fs::file_size(filePath));
    }
}

string CommandHandler::handleStor(Session& s, const string& arg) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";
    if (arg.empty()) return "501 Syntax error in parameters\r\n";

    fs::path target = fs::absolute(fs::path(s.getDir()) / arg);

    DataChannel dc(SERVER_DATA_PORT);
    if (!dc.start()) return "425 Can't open data connection\r\n";

    sendIntermediate("150 File status okay, opening data connection\r\n");

    bool ok = dc.receiveFile(target.string());
    dc.stop();

    return ok ? "226 Transfer complete\r\n" : "426 Connection closed, transfer aborted\r\n";
}

string CommandHandler::handleRetr(Session& s, const string& arg) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";
    if (arg.empty()) return "501 Syntax error in parameters\r\n";

    fs::path target = fs::absolute(fs::path(s.getDir()) / arg);
    if (!fs::exists(target) || !fs::is_regular_file(target))
        return format("550 File unavailable, {} not found\r\n", target.string());

    DataChannel dc(0);
    if (!dc.start()) return "425 Can't open data connection\r\n";

    sendIntermediate("150 File status okay, opening data connection\r\n");

    bool ok = dc.sendFile(target.string(), clientIp, CLIENT_DATA_PORT);
    dc.stop();

    return ok ? "226 Transfer complete\r\n" : "426 Connection closed, transfer aborted\r\n";
}

//Kiểm soát các lệnh cần gọi
string CommandHandler::handle(Session& s, const string& com, const string& arg) {
    //Xử lý các lệnh không tồn tại
    FtpCommand fc = toFtpCommand(com);
    if (fc == FtpCommand::UNKNOWN)
        return format("500 Syntax error, command unrecognized: '{}'\r\n", com);

    //Xử lý các lệnh đã/chưa được khai báo
    switch (fc) {
    case FtpCommand::USER: return handleUser(s, arg);
    case FtpCommand::PASS: return handlePass(s, arg);
    case FtpCommand::PWD:  return handlePwd(s);
    case FtpCommand::NOOP: return handleNoop();
    case FtpCommand::QUIT: return handleQuit();
    case FtpCommand::HELP: return handleHelp(arg);
    case FtpCommand::TYPE: return handleType(s, arg);
    case FtpCommand::MODE: return handleMode(s, arg);
    case FtpCommand::SIZE: return handleSize(s, arg);
    case FtpCommand::MDTM: return handleMdtm(s, arg);
    case FtpCommand::STAT: return handleStat(s, arg);
    case FtpCommand::STOR: return handleStor(s, arg);
    case FtpCommand::RETR: return handleRetr(s, arg);
    default: return "502 Command not implemented\r\n";
    }
}