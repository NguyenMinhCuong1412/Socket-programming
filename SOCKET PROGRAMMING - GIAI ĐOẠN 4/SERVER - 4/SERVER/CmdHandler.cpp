#include "CmdHandler.h"
#include "DataChannel.h"

void parseCmd(const string& raw, string& cmd, string& arg) {
    //Xử lý các kí tự có thể gây lỗi
    string clean = raw;
    while (!clean.empty() && (clean.back() == '\r' || clean.back() == '\n')) 
        clean.pop_back();

    //Tách lệnh và đối số
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

void CommandHandler::sendIntermediate(const string& msg) {  
    send(controlSocket, msg.c_str(), (int)msg.size(), 0); 
}

fs::path CommandHandler::resolvePath(const Session& s, const string& arg, string& outLogical) {
    fs::path logical = (!arg.empty() && arg[0] == '/')
        ? fs::path(arg)                        // arg tuyệt đối trong không gian FTP
        : fs::path(s.getDir()) / arg;          // arg tương đối so với thư mục hiện tại

    //lexically_normal không cho ".." vượt quá root
    string normStr = logical.lexically_normal().generic_string();
    if (normStr.empty()) normStr = "/";
    if (normStr[0] != '/') { 
        outLogical.clear(); 
        return fs::path(); 
    } //an toàn kép, phòng trường hợp lạ

    outLogical = normStr;

    //Map sang đường dẫn vật lý thật: SERVER_ROOT + phần sau dấu "/" đầu
    fs::path relativePart = (normStr == "/") ? fs::path() : fs::path(normStr.substr(1));
    return fs::weakly_canonical(SERVER_ROOT / relativePart); //weakly_canonical: OK cả khi path chưa tồn tại (MKD, STOR...)
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
        response += "    USER    PASS    PWD     NOOP    QUIT    HELP\n";
        response += "    TYPE    MODE    SIZE    STAT    MDTM    STOR\n";
        response += "    RETR    CWD     CDUP    MKD     RMD     LIST\n";
        response += "    NLST    STOU    APPE    DELE    RNFR    RNTO\n";
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
    case FtpCommand::CWD:  return "214 Syntax: CWD <path> - Change working directory\r\n";
    case FtpCommand::CDUP: return "214 Syntax: CDUP - Change to parent directory\r\n";
    case FtpCommand::MKD:  return "214 Syntax: MKD <dirname> - Create a new directory\r\n";
    case FtpCommand::RMD:  return "214 Syntax: RMD <dirname> - Remove an empty directory\r\n";
    case FtpCommand::LIST: return "214 Syntax: LIST - List files/directories with details\r\n";
    case FtpCommand::NLST: return "214 Syntax: NLST - List filenames only\r\n";
    case FtpCommand::STOU: return "214 Syntax: STOU - Upload a file, server chooses the filename\r\n";
    case FtpCommand::APPE: return "214 Syntax: APPE <filename> - Upload and append to an existing file\r\n";
    case FtpCommand::DELE: return "214 Syntax: DELE <filename> - Delete the specified file\r\n";
    case FtpCommand::RNFR: return "214 Syntax: RNFR <name> - Select file/dir to rename (step 1 of 2)\r\n";
    case FtpCommand::RNTO: return "214 Syntax: RNTO <newname> - Complete rename started by RNFR (step 2 of 2)\r\n";
    default: return format("501 Syntax error in parameters, unknown command '{}'\r\n", cmd);
    }
}

string CommandHandler::handleType(Session& s, const string& arg) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";
    string t = arg;
    for (auto& c : t) c = toupper(c);
    //Đều sử dụng BINARY để truyền - giản lược tối đa
    if (t != "A" && t != "I") return "501 Syntax error in parameters\r\n";
    s.setType(t);
    return format("200 Set type to {}\r\n", t);
}

string CommandHandler::handleMode(Session& s, const string& arg) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";
    string m = arg;
    for (auto& c : m) c = toupper(c);
    if (m == "B" || m == "C") return "502 Command not implemented\r\n";
    if (m != "S") return "501 Syntax error in parameters\r\n";
    s.setMode(m);
    return format("200 Set mode to {}\r\n", m);
}

string CommandHandler::handleSize(Session& s, const string& arg) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";
    if (arg.empty()) return "501 Syntax error in parameters\r\n";

    string logical;
    fs::path filePath = resolvePath(s, arg, logical);
    if (filePath.empty() || !fs::exists(filePath) || !fs::is_regular_file(filePath))
        return format("550 File unavailable, {} not found\r\n", arg);
    return format("213 {}\r\n", fs::file_size(filePath));
}

string CommandHandler::handleMdtm(Session& s, const string& arg) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";
    if (arg.empty()) return "501 Syntax error in parameters\r\n";

    string logical;
    fs::path filePath = resolvePath(s, arg, logical);
    if (filePath.empty() || !fs::exists(filePath))
        return format("550 File unavailable, {} not found\r\n", arg);

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
        string logical;
        fs::path filePath = resolvePath(s, arg, logical);
        if (filePath.empty() || !fs::exists(filePath))
            return format("550 File unavailable, {} not found\r\n", arg);
        if (fs::is_directory(filePath))
            return format("211 '{}' is a directory\r\n", logical);
        else return format("211 Size of {}: {}\r\n", logical, fs::file_size(filePath));
    }
}

string CommandHandler::handleStor(Session& s, const string& arg) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";
    if (arg.empty()) return "501 Syntax error in parameters\r\n";

    string logical;
    fs::path target = resolvePath(s, arg, logical);
    if (target.empty()) return "550 Invalid path\r\n";

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

    string logical;
    fs::path target = resolvePath(s, arg, logical);
    if (target.empty() || !fs::exists(target) || !fs::is_regular_file(target))
        return format("550 File unavailable, {} not found\r\n", arg);

    DataChannel dc(0);
    if (!dc.start()) return "425 Can't open data connection\r\n";

    sendIntermediate("150 File status okay, opening data connection\r\n");

    bool ok = dc.sendFile(target.string(), clientIp, CLIENT_DATA_PORT);
    dc.stop();

    return ok ? "226 Transfer complete\r\n" : "426 Connection closed, transfer aborted\r\n";
}

string CommandHandler::handleCwd(Session& s, const string& arg) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";
    if (arg.empty()) return "501 Syntax error in parameters\r\n";

    string newLogical;
    fs::path physical = resolvePath(s, arg, newLogical);

    if (physical.empty() || !fs::exists(physical) || !fs::is_directory(physical))
        return format("550 {} : No such directory\r\n", arg);

    s.setDir(newLogical);
    return format("250 Directory successfully changed to {}\r\n", newLogical);
}

string CommandHandler::handleCdup(Session& s) { return handleCwd(s, ".."); }

string CommandHandler::handleMkd(Session& s, const string& arg) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";
    if (arg.empty()) return "501 Syntax error in parameters\r\n";

    string newLogical;
    fs::path physical = resolvePath(s, arg, newLogical);
    if (physical.empty()) return "550 Invalid path\r\n";

    std::error_code ec;
    bool created = fs::create_directory(physical, ec); //chỉ tạo 1 cấp - nếu cha chưa có thì fail, đúng ngữ nghĩa MKD
    if (ec) return format("550 Can't create directory '{}' ({})\r\n", arg, ec.message());
    if (!created) return format("550 Directory '{}' already exists\r\n", arg);

    return format("257 \"{}\" created\r\n", newLogical);
}

string CommandHandler::handleRmd(Session& s, const string& arg) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";
    if (arg.empty()) return "501 Syntax error in parameters\r\n";

    string logical;
    fs::path physical = resolvePath(s, arg, logical);
    if (physical.empty() || !fs::exists(physical) || !fs::is_directory(physical))
        return format("550 {} : No such directory\r\n", arg);

    std::error_code ec;
    //fs::remove (khác remove_all) chỉ xóa được thư mục RỖNG - tự fail nếu còn file/thư mục con bên trong
    bool removed = fs::remove(physical, ec);
    if (ec || !removed)
        return format("550 Can't remove '{}', directory not empty\r\n", arg);

    return format("250 \"{}\" directory removed\r\n", arg);
}

//LIST/NLST: để đơn giản (project chưa cài PASV/PORT thật) kết quả được trả thẳng qua control
//socket giống HELP/STAT, thay vì qua DataChannel UDP riêng như STOR/RETR.
string CommandHandler::handleNlst(Session& s) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";

    string logical;
    fs::path physical = resolvePath(s, "", logical);
    if (physical.empty() || !fs::exists(physical))
        return "550 Directory not found\r\n";

    string body;
    for (auto& entry : fs::directory_iterator(physical))
        body += entry.path().filename().string() + "\r\n";

    return format("150 Here comes the directory listing\r\n{}226 Transfer complete\r\n", body);
}

string CommandHandler::handleList(Session& s) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";

    string logical;
    fs::path physical = resolvePath(s, "", logical);
    if (physical.empty() || !fs::exists(physical))
        return "550 Directory not found\r\n";

    string body;
    for (auto& entry : fs::directory_iterator(physical)) {
        bool isDir = entry.is_directory();
        uintmax_t size = isDir ? 0 : entry.file_size();
        body += format("{}\t{}\t{}\r\n", isDir ? "<DIR>" : "FILE", size, entry.path().filename().string());
    }

    return format("150 Here comes the directory listing\r\n{}226 Transfer complete\r\n", body);
}

string CommandHandler::handleStou(Session& s) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";

    //Server tự đặt tên file - KHÔNG dùng tên do client gửi (đúng ngữ nghĩa STOU)
    auto ms = chr::duration_cast<chr::milliseconds>(chr::system_clock::now().time_since_epoch()).count();
    string autoName = format("file_{}.dat", ms);

    string logical;
    fs::path target = resolvePath(s, autoName, logical);
    if (target.empty()) return "550 Invalid path\r\n";

    DataChannel dc(SERVER_DATA_PORT);
    if (!dc.start()) return "425 Can't open data connection\r\n";

    //Báo cho client biết tên file server đã chọn ngay trong response 150
    sendIntermediate(format("150 FILE: {}\r\n", autoName));

    bool ok = dc.receiveFile(target.string());
    dc.stop();

    return ok ? format("226 Transfer complete, stored as {}\r\n", autoName)
        : "426 Connection closed, transfer aborted\r\n";
}

string CommandHandler::handleAppe(Session& s, const string& arg) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";
    if (arg.empty()) return "501 Syntax error in parameters\r\n";

    string logical;
    fs::path target = resolvePath(s, arg, logical);
    if (target.empty()) return "550 Invalid path\r\n";

    DataChannel dc(SERVER_DATA_PORT);
    if (!dc.start()) return "425 Can't open data connection\r\n";

    sendIntermediate("150 File status okay, opening data connection\r\n");

    bool ok = dc.receiveFile(target.string(), true); //append = true -> mở file bằng ios::app
    dc.stop();

    return ok ? "226 Transfer complete\r\n" : "426 Connection closed, transfer aborted\r\n";
}

string CommandHandler::handleDele(Session& s, const string& arg) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";
    if (arg.empty()) return "501 Syntax error in parameters\r\n";

    string logical;
    fs::path target = resolvePath(s, arg, logical);
    if (target.empty() || !fs::exists(target) || !fs::is_regular_file(target))
        return format("550 {} : No such file\r\n", arg);

    std::error_code ec;
    bool removed = fs::remove(target, ec);
    if (ec || !removed)
        return format("550 Can't delete '{}'\r\n", arg);

    return format("250 \"{}\" deleted\r\n", arg);
}

string CommandHandler::handleRnfr(Session& s, const string& arg) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";
    if (arg.empty()) return "501 Syntax error in parameters\r\n";

    string logical;
    fs::path target = resolvePath(s, arg, logical);
    if (target.empty() || !fs::exists(target))
        return format("550 {} : No such file or directory\r\n", arg);

    s.setRenameFrom(arg); //lưu tên gốc (dạng arg gốc) để RNTO tự resolve lại
    return "350 Requested file action pending further information\r\n";
}

string CommandHandler::handleRnto(Session& s, const string& arg) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";
    if (arg.empty()) return "501 Syntax error in parameters\r\n";

    //Chưa có RNFR trước đó -> lỗi trình tự lệnh
    if (s.getRenameFrom().empty())
        return "503 Bad sequence of commands\r\n";

    string oldLogical, newLogical;
    fs::path oldPath = resolvePath(s, s.getRenameFrom(), oldLogical);
    fs::path newPath = resolvePath(s, arg, newLogical);

    s.setRenameFrom(""); //clear state ngay, dù thành công hay thất bại - tránh RNTO kế tiếp dùng nhầm

    if (oldPath.empty() || newPath.empty() || !fs::exists(oldPath))
        return "550 Rename failed\r\n";

    std::error_code ec;
    fs::rename(oldPath, newPath, ec);
    if (ec) return format("550 Rename failed ({})\r\n", ec.message());

    return "250 Rename successful\r\n";
}

//Điều phối lệnh
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
    case FtpCommand::CWD:  return handleCwd(s, arg);
    case FtpCommand::CDUP: return handleCdup(s);
    case FtpCommand::MKD:  return handleMkd(s, arg);
    case FtpCommand::RMD:  return handleRmd(s, arg);
    case FtpCommand::LIST: return handleList(s);
    case FtpCommand::NLST: return handleNlst(s);
    case FtpCommand::STOU: return handleStou(s);
    case FtpCommand::APPE: return handleAppe(s, arg);
    case FtpCommand::DELE: return handleDele(s, arg);
    case FtpCommand::RNFR: return handleRnfr(s, arg);
    case FtpCommand::RNTO: return handleRnto(s, arg);
    default: return "502 Command not implemented\r\n";
    }
}