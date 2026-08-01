#include "CmdHandler.h"
#include "DataChannel.h"

void parseCmd(const string& raw, string& cmd, string& arg) {
    //Xử lý các kí tự có thể gây lỗi
    string clean = raw;
    while (!clean.empty() && (clean.back() == '\r' || clean.back() == '\n')) clean.pop_back();

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

void CommandHandler::joinPreviousTransfer() {
    if (transferThread.joinable()) //Kiểm tra luồng phụ còn hoạt động 
        transferThread.join();     //Bắt chương trình chờ xong xuôi
}

void CommandHandler::sendIntermediate(const string& msg) { send(clientSocket, msg.c_str(), (int)msg.size(), 0); }

fs::path CommandHandler::resolvePath(const Session& s, const string& arg, string& outLogical) {
    //Xác định tính tuyệt đối/tương đối của filename
    fs::path logical = (!arg.empty() && arg[0] == '/')
        ? fs::path(arg)               //Đường dẫn tuyệt đối - đầy đủ bắt đầu từ gốc hệ thống tệp (root directory) cho đến vị trí tệp/thư mục trên ổ cứng
        : fs::path(s.getDir()) / arg; //Đường dẫn tương đối - dựa trên vị trí hiện tại của chương trình đang chạy

    //Chuẩn hóa đường dẫn 
    //lexically_normal không cho ".." vượt quá root
    //generic_string: chuyển đổi path -> string
    string normStr = logical.lexically_normal().generic_string();
    if (normStr.empty()) normStr = "/";
    if (normStr[0] != '/') {
        outLogical.clear();
        return fs::path();
    } //an toàn kép, phòng trường hợp lạ

    outLogical = normStr;

    //Map sang đường dẫn vật lý thật: SERVER_ROOT + phần sau dấu "/" đầu
    //Chặn path traversal 
    fs::path relativePart = (normStr == "/") ? fs::path() : fs::path(normStr.substr(1));
    return fs::weakly_canonical(SERVER_ROOT / relativePart); //weakly_canonical: OK cả khi path chưa tồn tại
}

unsigned short CommandHandler::pickListenPort(Session& s) {
    return (s.getDataMode() == DataMode::PASSIVE) ? s.getPassivePort() : SERVER_DATA_PORT;
}

CommandHandler::CommandHandler() {
    this->clientSocket = INVALID_SOCKET;
    this->clientIp = "";
}

CommandHandler::~CommandHandler() { joinPreviousTransfer(); }

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
    if (arg.empty()) {  //Hiển thị toàn bộ lệnh hỗ trợ
        string response = "214 The following commands are recognized:\r\n";
        response += "    USER    PASS    PWD     NOOP    QUIT    HELP\n";
        response += "    TYPE    MODE    SIZE    STAT    MDTM    STOR\n";
        response += "    RETR    CWD     CDUP    MKD     RMD     LIST\n";
        response += "    NLST    STOU    APPE    DELE    RNFR    RNTO\n";
        response += "    PORT    PASV    ABOR\n";
        response += "214 Direct queries to this command using 'HELP <command>'.\r\n";
        return response;
    }

    //Tra cứu chi tiết lệnh
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
    case FtpCommand::PORT: return "214 Syntax: PORT h1,h2,h3,h4,p1,p2 - Tell server client's active-mode address\r\n";
    case FtpCommand::PASV: return "214 Syntax: PASV - Ask server to open a passive-mode data port\r\n";
    case FtpCommand::ABOR: return "214 Syntax: ABOR - Abort the transfer currently in progress\r\n";
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

    auto ftime = fs::last_write_time(filePath);            //Định dạng file_time_type
    auto sctp = chr::clock_cast<chr::system_clock>(ftime); //Định dạng time_point: ép file_time_type về mốc lịch sử chung mà hàm C++ hiểu
    auto tt = chr::system_clock::to_time_t(sctp);          //Định dạng time_t: tổng giây từ 00:00:00 ngày 01/01/1970

    tm time; localtime_s(&time, &tt);

    return format("213 {:04}{:02}{:02}{:02}{:02}{:02}\r\n", //Định dạng YYYYMMDDhhmmss
        time.tm_year + 1900, time.tm_mon + 1, time.tm_mday,
        time.tm_hour, time.tm_min, time.tm_sec);
}

string CommandHandler::handleStat(Session& s, const string& arg) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";
    if (arg.empty()) { //Trạng thái chung 
        return "211 FTP server status:\r\n"
            " User: " + s.getUserName() + "\r\n"
            " Current working directory: " + s.getDir() + "\r\n"
            " Data type: " + s.getType() + "\r\n"
            " Transfer mode: " + s.getMode() + "\r\n"
            "211 End of status\r\n";
    }
    else { //Trạng thái của file
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

    joinPreviousTransfer();

    //Khởi tạo kênh dữ liệu - UDP (Server nhận - Client gửi)
    auto dc = make_shared<DataChannel>(pickListenPort(s));
    if (!dc->start()) return "425 Can't open data connection\r\n";
    sendIntermediate("150 File status okay, opening data connection\r\n"); 
    s.setActiveDataChannel(dc.get()); //Đăng ký để ABOR (thread khác) có thể đóng socket này

    //Chạy transfer thật ở luồng phụ -> luồng chính (đang đọc lệnh) rảnh ngay để nhận lệnh mới
    transferThread = thread([this, &s, dc, target]() {
        bool ok = dc->receiveFile(target.string()); //Server nhận dữ liệu từ Client
        s.setActiveDataChannel(nullptr);
        dc->stop();
        this->sendIntermediate(ok ? "226 Transfer complete\r\n" : "426 Connection closed, transfer aborted\r\n");
        });

    return ""; 
}

string CommandHandler::handleRetr(Session& s, const string& arg) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";
    if (arg.empty()) return "501 Syntax error in parameters\r\n";

    string logical;
    fs::path target = resolvePath(s, arg, logical);
    if (target.empty() || !fs::exists(target) || !fs::is_regular_file(target))
        return format("550 File unavailable, {} not found\r\n", arg);

    joinPreviousTransfer();

    DataMode mode = s.getDataMode();
    //PASSIVE: Server đã hẹn port trước (qua PASV) -> bind đúng port đó, chờ handshake để đọc địa chỉ Client.
    //ACTIVE/NONE: Server tự chọn port ngẫu nhiên (0), đích lấy từ PORT hoặc mặc định
    unsigned short listenPort = (mode == DataMode::PASSIVE) ? s.getPassivePort() : 0;

    //Khởi tạo kênh dữ liệu - UDP (Server gửi - Client nhận)
    auto dc = make_shared<DataChannel>(listenPort);
    if (!dc->start()) return "425 Can't open data connection\r\n";
    sendIntermediate("150 File status okay, opening data connection\r\n"); 
    s.setActiveDataChannel(dc.get());

    string destIp = (mode == DataMode::ACTIVE) ? s.getActiveIp() : clientIp;
    unsigned short destPort = (mode == DataMode::ACTIVE) ? s.getActivePort() : CLIENT_DATA_PORT;

    transferThread = thread([this, &s, dc, target, mode, destIp, destPort]() {
        bool ok = (mode == DataMode::PASSIVE)
            ? dc->sendFileAfterHandshake(target.string())
            : dc->sendFile(target.string(), destIp, destPort); //Server gửi dữ liệu tới Client
        s.setActiveDataChannel(nullptr);
        dc->stop();
        this->sendIntermediate(ok ? "226 Transfer complete\r\n" : "426 Connection closed, transfer aborted\r\n");
        });

    return "";
}

string CommandHandler::handleCwd(Session& s, const string& arg) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";
    if (arg.empty()) return "501 Syntax error in parameters\r\n";

    string newLogical;
    fs::path physical = resolvePath(s, arg, newLogical);

    if (physical.empty() || !fs::exists(physical) || !fs::is_directory(physical))
        return format("550 {} : No such directory\r\n", arg);

    s.setDir(newLogical); //Thay đổi đường dẫn làm việc của Client trên Server
    return format("250 Directory successfully changed to {}\r\n", newLogical);
}

string CommandHandler::handleCdup(Session& s) { return handleCwd(s, ".."); }

string CommandHandler::handleMkd(Session& s, const string& arg) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";
    if (arg.empty()) return "501 Syntax error in parameters\r\n";

    string newLogical;
    fs::path physical = resolvePath(s, arg, newLogical);
    if (physical.empty()) return "550 Invalid path\r\n";

    error_code ec; //Chứa mã lỗi hệ thống được trả về an toàn - tránh crash (vừa khai báo = không có lỗi)
    bool created = fs::create_directory(physical, ec); //create_directory: tạo 1 thư mục tại đường dãn thực tế đúng 1 cấp - nếu cha chưa có thì fail
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

    error_code ec;
    bool removed = fs::remove(physical, ec); //remove (khác remove_all) chỉ xóa được thư mục RỖNG - tự fail nếu còn file/thư mục con bên trong
    if (ec || !removed) return format("550 Can't remove '{}', directory not empty\r\n", arg);
    return format("250 \"{}\" directory removed\r\n", arg);
}

string CommandHandler::handleNlst(Session& s) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";

    string logical;
    fs::path physical = resolvePath(s, "", logical);
    if (physical.empty() || !fs::exists(physical)) return "550 Directory not found\r\n";

    string body;
    for (auto& entry : fs::directory_iterator(physical))   //directory_iterator: duyệt danh sách tệp/thư mục con nằm trong 
        body += entry.path().filename().string() + "\r\n"; //entry.path(): trả về đường dẫn của từng file/thư mục con bên trong 
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
        bool isDir = entry.is_directory();              //Kiểm tra thư mục hay tệp tin
        uintmax_t size = isDir ? 0 : entry.file_size(); //uintmax_t: unsigned integer có kích thước lớn nhất hệ thống C++ hỗ trợ
        body += format("{}\t{}\t{}\r\n", isDir ? "<DIR>" : "FILE", size, entry.path().filename().string());
    }
    return format("150 Here comes the directory listing\r\n{}226 Transfer complete\r\n", body);
}

string CommandHandler::handleStou(Session& s) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";

    //Server tự đặt tên file - KHÔNG dùng tên do client gửi
    auto ms = chr::duration_cast<chr::milliseconds>(chr::system_clock::now().time_since_epoch()).count();
    /*
    system_clock::now(): lấy thời gian thực tế hiện tại của hệ thống máy tính
    time_since_epoch(): tính khoảng thời gian trôi qua kể từ mốc Unix Epoch (C++, thường là 00:00:00 1/1/1970)
    duration_cast<chr::milliseconds>(...): ép kiểu khoảng thời gian vừa lấy được sang đơn vị mili-giây
    count(): Lấy ra giá trị số nguyên đại diện cho tổng số mili-giây đó
    */
    string autoName = format("file_{}.dat", ms);

    string logical;
    fs::path target = resolvePath(s, autoName, logical);
    if (target.empty()) return "550 Invalid path\r\n";

    joinPreviousTransfer();

    //Khởi tạo kênh dữ liệu - UDP (Server nhận - Client gửi)
    auto dc = make_shared<DataChannel>(pickListenPort(s));
    if (!dc->start()) return "425 Can't open data connection\r\n";
    sendIntermediate(format("150 FILE: {}\r\n", autoName));  //Báo cho client biết tên file server đã chọn
    s.setActiveDataChannel(dc.get());

    transferThread = thread([this, &s, dc, target, autoName]() {
        bool ok = dc->receiveFile(target.string()); //Server nhận dữ liệu từ Client
        s.setActiveDataChannel(nullptr);
        dc->stop();
        this->sendIntermediate(ok ? format("226 Transfer complete, stored as {}\r\n", autoName)
            : string("426 Connection closed, transfer aborted\r\n"));
        });

    return "";
}

string CommandHandler::handleAppe(Session& s, const string& arg) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";
    if (arg.empty()) return "501 Syntax error in parameters\r\n";

    string logical;
    fs::path target = resolvePath(s, arg, logical);
    if (target.empty()) return "550 Invalid path\r\n";

    joinPreviousTransfer();

    //Khởi tạo kênh dữ liệu - UDP (Server nhận - Client gửi)
    auto dc = make_shared<DataChannel>(pickListenPort(s));
    if (!dc->start()) return "425 Can't open data connection\r\n";
    sendIntermediate("150 File status okay, opening data connection\r\n");
    s.setActiveDataChannel(dc.get());

    transferThread = thread([this, &s, dc, target]() {
        bool ok = dc->receiveFile(target.string(), true); //append = true -> mở file bằng ios::app
        s.setActiveDataChannel(nullptr);
        dc->stop();
        this->sendIntermediate(ok ? "226 Transfer complete\r\n" : "426 Connection closed, transfer aborted\r\n");
        });

    return "";
}

string CommandHandler::handleDele(Session& s, const string& arg) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";
    if (arg.empty()) return "501 Syntax error in parameters\r\n";

    string logical;
    fs::path target = resolvePath(s, arg, logical);
    if (target.empty() || !fs::exists(target) || !fs::is_regular_file(target))
        return format("550 {} : No such file\r\n", arg);

    error_code ec;
    bool removed = fs::remove(target, ec);
    if (ec || !removed) return format("550 Can't delete '{}'\r\n", arg);
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
    if (s.getRenameFrom().empty()) return "503 Bad sequence of commands\r\n";

    string oldLogical, newLogical;
    fs::path oldPath = resolvePath(s, s.getRenameFrom(), oldLogical);
    fs::path newPath = resolvePath(s, arg, newLogical);

    s.setRenameFrom(""); //clear state ngay, dù thành công hay thất bại - tránh RNTO kế tiếp dùng nhầm

    if (oldPath.empty() || newPath.empty() || !fs::exists(oldPath)) return "550 Rename failed\r\n";

    error_code ec;
    fs::rename(oldPath, newPath, ec);
    if (ec) return format("550 Rename failed ({})\r\n", ec.message());
    return "250 Rename successful\r\n";
}

string CommandHandler::handlePort(Session& s, const string& arg) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";

    //Cú pháp: h1,h2,h3,h4,p1,p2 -> IP = h1.h2.h3.h4 ; port = p1*256 + p2
    vector<int> nums;
    stringstream ss(arg);
    string token;
    while (getline(ss, token, ',')) {
        try { nums.push_back(stoi(token)); }
        catch (...) { return "501 Syntax error in parameters\r\n"; }
    }
    if (nums.size() != 6) return "501 Syntax error in parameters\r\n";
    for (int n : nums) if (n < 0 || n > 255) return "501 Syntax error in parameters\r\n";

    string ip = format("{}.{}.{}.{}", nums[0], nums[1], nums[2], nums[3]);
    unsigned short port = (unsigned short)(nums[4] * 256 + nums[5]);

    s.setActiveMode(ip, port);
    return "200 PORT command successful\r\n";
}

string CommandHandler::handlePasv(Session& s) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";

    //Chọn port tuần tự có wrap-around trong dải 6000-6999, thread-safe vì nhiều client
    //(nhiều thread) có thể gọi PASV cùng lúc.
    static atomic<unsigned short> nextPort{ 6000 };
    unsigned short port = nextPort.fetch_add(1);
    if (port > 6999) { 
        nextPort.store(6000); 
        port = 6000; }

    s.setPassiveMode(port);

    //Lấy IP thật của server theo góc nhìn của client này, từ chính control socket đang kết nối
    sockaddr_in localAddr{};
    int len = sizeof(localAddr);
    getsockname(clientSocket, (sockaddr*)&localAddr, &len);
    char ipStr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &localAddr.sin_addr, ipStr, INET_ADDRSTRLEN);

    vector<int> ipParts;
    stringstream ipss(ipStr);
    string seg;
    while (getline(ipss, seg, '.')) ipParts.push_back(std::stoi(seg));
    while (ipParts.size() < 4) ipParts.push_back(0);

    return format("227 Entering Passive Mode ({},{},{},{},{},{})\r\n",
        ipParts[0], ipParts[1], ipParts[2], ipParts[3], port / 256, port % 256);
}

string CommandHandler::handleAbor(Session& s) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";

    //QUAN TRỌNG: gửi 225 TRƯỚC khi đóng socket, để client luôn thấy "225 ..." rồi mới tới
    //"426 ..." (thread phụ tự gửi khi phát hiện socket bị đóng).
    sendIntermediate("225 ABOR command successful\r\n");
    s.abortActiveTransfer();
    return "";
}

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
    case FtpCommand::PORT: return handlePort(s, arg);
    case FtpCommand::PASV: return handlePasv(s);
    case FtpCommand::ABOR: return handleAbor(s);
    default: return "502 Command not implemented\r\n";
    }
}