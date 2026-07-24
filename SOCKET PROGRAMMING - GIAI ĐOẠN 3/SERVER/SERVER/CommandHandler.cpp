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

CommandHandler::CommandHandler(const string& clientIp) : clientIp(clientIp) {}

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
        response += "    USER PASS PWD  NOOP QUIT HELP TYPE MODE\r\n";
        response += "    SIZE MDTM STAT STOR RETR\r\n";
        response += "214 Direct queries to this command using 'HELP <command>'.\r\n";
        return response;
    }
    string cmd = arg;
    for (auto& c : cmd) c = toupper(c);

    switch (toFtpCommand(cmd)) {
    case FtpCommand::USER: return "214 Syntax: USER <username>\r\n";
    case FtpCommand::PASS: return "214 Syntax: PASS <password>\r\n";
    case FtpCommand::PWD:  return "214 Syntax: PWD\r\n";
    case FtpCommand::NOOP: return "214 Syntax: NOOP\r\n";
    case FtpCommand::QUIT: return "214 Syntax: QUIT\r\n";
    case FtpCommand::HELP: return "214 Syntax: HELP [command]\r\n";
    case FtpCommand::TYPE: return "214 Syntax: TYPE {A|I}\r\n";
    case FtpCommand::MODE: return "214 Syntax: MODE {S|B|C}\r\n";
    case FtpCommand::SIZE: return "214 Syntax: SIZE <filename>\r\n";
    case FtpCommand::MDTM: return "214 Syntax: MDTM <filename>\r\n";
    case FtpCommand::STAT: return "214 Syntax: STAT [path]\r\n";
    case FtpCommand::STOR: return "214 Syntax: STOR <filename>\r\n";
    case FtpCommand::RETR: return "214 Syntax: RETR <filename>\r\n";
    default:
        return format("501 Syntax error in parameters, unknown command '{}'\r\n", cmd);
    }
}

string CommandHandler::handleType(Session& s, const string& arg) {
    string t = arg;
    for (auto& c : t) c = toupper(c);
    if (t != "A" && t != "I") return "501 Syntax error in parameters\r\n";
    s.setDataType(t);
    return format("200 Type set to {}\r\n", t);
}

string CommandHandler::handleMode(Session& s, const string& arg) {
    string m = arg;
    for (auto& c : m) c = toupper(c);
    if (m != "S" && m != "B" && m != "C") return "501 Syntax error in parameters\r\n";
    if (m != "S") return "502 Command not implemented, only Stream mode supported\r\n";
    s.setTransferMode(m);
    return format("200 Mode set to {}\r\n", m);
}

string CommandHandler::handleSize(const string& arg) {
    if (arg.empty()) return "501 Syntax error in parameters\r\n";
    if (!fs::exists(arg) || !fs::is_regular_file(arg))
        return format("550 File unavailable, '{}' not found\r\n", arg);
    return format("213 {}\r\n", fs::file_size(arg));
}

string CommandHandler::handleMdtm(const string& arg) {
    if (arg.empty()) return "501 Syntax error in parameters\r\n";
    if (!fs::exists(arg))
        return format("550 File unavailable, '{}' not found\r\n", arg);

    auto ftime = fs::last_write_time(arg);
    auto sctp = std::chrono::clock_cast<std::chrono::system_clock>(ftime);
    std::time_t tt = std::chrono::system_clock::to_time_t(sctp);
    std::tm tm;
    localtime_s(&tm, &tt);

    return format("213 {:04}{:02}{:02}{:02}{:02}{:02}\r\n",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tm.tm_sec);
}

string CommandHandler::handleStat(const string& arg) {
    string path = arg.empty() ? "." : arg;
    if (!fs::exists(path)) return format("450 File unavailable, '{}' not found\r\n", path);

    if (fs::is_directory(path)) {
        return format("211 '{}' is a directory\r\n", path);
    }
    return format("211 '{}' size={} bytes\r\n", path, fs::file_size(path));
}

string CommandHandler::handleStor(Session& s, const string& arg) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";
    if (arg.empty()) return "501 Syntax error in parameters\r\n";

    // Phản hồi trung gian rồi tiến hành nhận file luôn (đồng bộ, chưa đa luồng)
    cout << "150 File status okay, opening data connection" << endl;

    DataChannel channel(SERVER_DATA_PORT);
    if (!channel.open()) return "425 Can't open data connection\r\n";

    bool ok = channel.receiveFile(arg);
    channel.close();

    return ok ? "226 Transfer complete\r\n" : "426 Connection closed, transfer aborted\r\n";
}

string CommandHandler::handleRetr(Session& s, const string& arg) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";
    if (arg.empty()) return "501 Syntax error in parameters\r\n";
    if (!fs::exists(arg) || !fs::is_regular_file(arg))
        return format("550 File unavailable, '{}' not found\r\n", arg);

    cout << "150 File status okay, opening data connection" << endl;

    DataChannel channel(0); // cổng nguồn tự động, chỉ dùng để gửi
    if (!channel.open()) return "425 Can't open data connection\r\n";

    bool ok = channel.sendFile(arg, clientIp, CLIENT_DATA_PORT);
    channel.close();

    return ok ? "226 Transfer complete\r\n" : "426 Connection closed, transfer aborted\r\n";
}

string CommandHandler::handle(Session& s, const string& com, const string& arg) {
    switch (toFtpCommand(com)) {
    case FtpCommand::USER: return handleUser(s, arg);
    case FtpCommand::PASS: return handlePass(s, arg);
    case FtpCommand::PWD:  return handlePwd(s);
    case FtpCommand::NOOP: return handleNoop();
    case FtpCommand::QUIT: return handleQuit();
    case FtpCommand::HELP: return handleHelp(arg);
    case FtpCommand::TYPE: return handleType(s, arg);
    case FtpCommand::MODE: return handleMode(s, arg);
    case FtpCommand::SIZE: return handleSize(arg);
    case FtpCommand::MDTM: return handleMdtm(arg);
    case FtpCommand::STAT: return handleStat(arg);
    case FtpCommand::STOR: return handleStor(s, arg);
    case FtpCommand::RETR: return handleRetr(s, arg);
    default:
        return "502 Command not implemented\r\n";
    }
}