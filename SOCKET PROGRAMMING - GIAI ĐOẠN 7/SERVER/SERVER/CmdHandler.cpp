// ======================================================================
// CmdHandler.cpp — CÀI ĐẶT BỘ XỬ LÝ LỆNH FTP CỦA SERVER
//    Triển khai toàn bộ 27 lệnh FTP: xác thực (USER/PASS), quản lý
//    thư mục (CWD/CDUP/MKD/RMD/LIST/NLST), truyền file (STOR/RETR/
//    STOU/APPE), thông tin (SIZE/MDTM/STAT/HASH), và điều khiển
//    kết nối dữ liệu (PORT/PASV/ABOR).
//    Mỗi lệnh truyền dữ liệu tạo DataChannel tạm thời chạy trên
//    thread riêng (transferThread) để không block kênh điều khiển TCP.
// ======================================================================
#include "CmdHandler.h"
#include "DataChannel.h"
#include "HashUtil.h"

// Tách dòng lệnh thô thành tên lệnh (cmd) và tham số (arg)
// Xóa '\r', '\n' ở cuối → tách bằng khoảng trắng đầu tiên
// cmd được chuyển thành CHỮ HOA (chuẩn FTP không phân biệt hoa/thường)
void parseCmd(const string& raw, string& cmd, string& arg) {
    string clean = raw;
    while (!clean.empty() && (clean.back() == '\r' || clean.back() == '\n')) clean.pop_back();

    istringstream iss(clean);
    iss >> cmd;                // Đọc từ đầu tiên = tên lệnh
    getline(iss, arg);         // Phần còn lại = tham số
    if (!arg.empty() && arg[0] == ' ') arg = arg.substr(1);  // Xóa khoảng trắng đầu tham số
    for (auto& c : cmd) c = toupper(c);  // Chuyển tên lệnh thành chữ hoa
}

// Chuyển tên lệnh (string) → enum FtpCommand để dùng trong switch-case
// Trả về FtpCommand::UNKNOWN nếu không nhận diện được
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

// Chờ thread transfer trước đó hoàn tất (join) trước khi bắt đầu transfer mới
// Tránh race condition khi 2 lệnh truyền dữ liệu liên tiếp
void CommandHandler::joinPreviousTransfer() {
    if (transferThread.joinable())
        transferThread.join();
}

// Gửi phản hồi trung gian lên kênh điều khiển TCP — dùng cho các mã 150, 226, 426...
// Được gọi từ thread transfer (không phải thread xử lý lệnh chính)
void CommandHandler::sendIntermediate(const string& msg) { send(clientSocket, msg.c_str(), (int)msg.size(), 0); }

// =====================================================================
// resolvePath — PHÂN GIẢI ĐƯỜNG DẪN LOGIC → VẬT LÝ
//   Chuyển đường dẫn FTP logic (vd: "/dir/file.txt") thành đường dẫn
//   vật lý trên hệ thống file (vd: "server_root/dir/file.txt").
//   Kiểm tra path traversal: đảm bảo đường dẫn vật lý không thoát
//   khỏi SERVER_ROOT (chống truy cập file ngoài thư mục FTP).
//   Trả về path rỗng nếu đường dẫn không hợp lệ hoặc thoát khỏi root.
// =====================================================================
fs::path CommandHandler::resolvePath(const Session& s, const string& arg, string& outLogical) {
    // Nếu arg bắt đầu bằng '/' → đường dẫn tuyệt đối, ngược lại → tương đối từ thư mục hiện tại
    fs::path logical = (!arg.empty() && arg[0] == '/')
        ? fs::path(arg)
        : fs::path(s.getDir()) / arg;

    // Chuẩn hóa: xử lý "..", "." và chuyển sang dạng generic (dùng '/')
    string normStr = logical.lexically_normal().generic_string();
    if (normStr.empty()) normStr = "/";
    if (normStr[0] != '/') {
        outLogical.clear();
        return fs::path();  // Đường dẫn không hợp lệ
    }

    outLogical = normStr;

    // Chuyển logical path → physical path: bỏ '/' đầu, nối với SERVER_ROOT
    fs::path relativePart = (normStr == "/") ? fs::path() : fs::path(normStr.substr(1));
    fs::path physical = fs::weakly_canonical(SERVER_ROOT / relativePart);

    // === Kiểm tra path traversal (chống thoát khỏi SERVER_ROOT) ===
    fs::path canonicalRoot = fs::canonical(SERVER_ROOT);
    string rootStr = canonicalRoot.generic_string();
    string physStr = physical.generic_string();

    // So sánh case-insensitive (Windows không phân biệt hoa/thường trong đường dẫn)
    string rootLower = rootStr;
    for (auto& c : rootLower) c = tolower((unsigned char)c);
    string physLower = physStr;
    for (auto& c : physLower) c = tolower((unsigned char)c);

    // Kiểm tra: physical path phải bắt đầu bằng root path
    if (physLower.size() < rootLower.size() ||
        physLower.compare(0, rootLower.size(), rootLower) != 0 ||
        (physLower.size() > rootLower.size() && physStr[rootLower.size()] != '/')) {
        outLogical.clear();
        return fs::path();  // Path traversal detected → từ chối
    }

    return physical;
}

// Chọn port bind cho DataChannel:
// Passive mode → dùng passivePort (Server đã thông báo cho Client)
// Active mode → dùng 0 (OS tự chọn port ngẫu nhiên)
unsigned short CommandHandler::pickListenPort(Session& s) {
    return (s.getDataMode() == DataMode::PASSIVE) ? s.getPassivePort() : 0;
}

// Gắn thêm "PORT=xxx" vào phản hồi 150 nếu đang ở Active mode
// Client cần biết port Server đã bind để gửi file đến port đó
// Passive mode không cần (Client đã biết port từ phản hồi 227)
string CommandHandler::appendPortIfNeeded(Session& s, unsigned short boundPort, const string& baseMsg) {
    if (s.getDataMode() == DataMode::PASSIVE) return baseMsg;

    string msg = baseMsg;
    size_t pos = msg.rfind("\r\n");
    string suffix = format(" PORT={}", boundPort);
    if (pos != string::npos) msg.insert(pos, suffix);  // Chèn trước "\r\n" cuối
    else msg += suffix;
    return msg;
}

// Constructor
CommandHandler::CommandHandler() {
    this->clientIp = "";
    this->clientSocket = INVALID_SOCKET;
}

// Destructor: chờ thread transfer hoàn tất
CommandHandler::~CommandHandler() { joinPreviousTransfer(); }

// === handleUser: nhận tên người dùng ===
// Reset trạng thái đăng nhập trước (cho phép đăng nhập lại với user khác)
string CommandHandler::handleUser(Session& s, const string& arg) {
    if (arg.empty()) return "501 Syntax error in parameters\r\n";
    s.setLoggedIn(false);      // Đặt lại trạng thái chưa đăng nhập
    s.setRenameFrom("");       // Xóa trạng thái đổi tên đang chờ
    s.setUserName(arg);        // Lưu tên người dùng
    return "331 Username OK, need password\r\n";  // 331: cần mật khẩu
}

// === handlePass: nhận mật khẩu (chấp nhận mọi mật khẩu — simplified FTP) ===
string CommandHandler::handlePass(Session& s, const string& arg) {
    if (s.getUserName().empty()) return "503 Bad sequence of commands\r\n";  // Chưa gửi USER
    if (arg.empty()) return "501 Syntax error in parameters\r\n";
    s.setLoggedIn(true);  // Đăng nhập thành công (không kiểm tra mật khẩu)
    return "230 Login successful\r\n";  // 230: đăng nhập thành công
}

// === handlePwd: in thư mục làm việc hiện tại ===
string CommandHandler::handlePwd(Session& s) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";
    return format("257 \"{}\" is current directory\r\n", s.getDir());  // 257: đường dẫn hiện tại
}

// === handleNoop: không làm gì — dùng làm keep-alive ===
string CommandHandler::handleNoop() { return "200 NOOP OK\r\n"; }

// === handleQuit: kết thúc phiên ===
string CommandHandler::handleQuit() {
	joinPreviousTransfer();  // Chờ transfer hoàn tất trước khi đóng
    return "221 Goodbye\r\n";  // 221: kết thúc phiên
}

// === handleHelp: trợ giúp ===
// Không tham số → liệt kê tất cả lệnh hỗ trợ
// Có tham số → mô tả cú pháp lệnh cụ thể
string CommandHandler::handleHelp(const string& arg) {
    if (arg.empty()) {
        string response = "214- The following commands are recognized:\r\n";
        response += "    USER    PASS    PWD     NOOP    QUIT    HELP\n";
        response += "    TYPE    MODE    SIZE    STAT    MDTM    STOR\n";
        response += "    RETR    CWD     CDUP    MKD     RMD     LIST\n";
        response += "    NLST    STOU    APPE    DELE    RNFR    RNTO\n";
        response += "    PORT    PASV    ABOR    HASH\n";
        response += "214 Direct queries to this command using 'HELP <command>'.\r\n";
        return response;
    }

    // Tra cứu cú pháp lệnh cụ thể
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
    case FtpCommand::HASH: return "214 Syntax: HASH <filename> - Return SHA-256 hash of the file for integrity verification\r\n";
    case FtpCommand::PORT: return "214 Syntax: PORT h1,h2,h3,h4,p1,p2 - Tell server client's active-mode address\r\n";
    case FtpCommand::PASV: return "214 Syntax: PASV - Ask server to open a passive-mode data port\r\n";
    case FtpCommand::ABOR: return "214 Syntax: ABOR - Abort the transfer currently in progress\r\n";
    default: return format("501 Syntax error in parameters, unknown command '{}'\r\n", cmd);
    }
}

// === handleType: đặt chế độ truyền dữ liệu ===
// A = ASCII (chuyển đổi line ending), I = Image/Binary (truyền nguyên bản)
string CommandHandler::handleType(Session& s, const string& arg) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";
    string t = arg;
    for (auto& c : t) c = toupper(c);
    if (t != "A" && t != "I") return "501 Syntax error in parameters\r\n";
    s.setType(t);
    return format("200 Set type to {}\r\n", t);
}

// === handleMode: đặt chế độ truyền ===
// S = Stream (duy nhất hỗ trợ), B = Block và C = Compressed (không hỗ trợ → 502)
string CommandHandler::handleMode(Session& s, const string& arg) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";
    string m = arg;
    for (auto& c : m) c = toupper(c);
    if (m == "B" || m == "C") return "502 Command not implemented\r\n";  // Block/Compressed chưa cài đặt
    if (m != "S") return "501 Syntax error in parameters\r\n";
    s.setMode(m);
    return format("200 Set mode to {}\r\n", m);
}

// === handleSize: trả về kích thước file (byte) ===
string CommandHandler::handleSize(Session& s, const string& arg) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";
    if (arg.empty()) return "501 Syntax error in parameters\r\n";

    string logical;
    fs::path filePath = resolvePath(s, arg, logical);
    if (filePath.empty() || !fs::exists(filePath) || !fs::is_regular_file(filePath))
        return format("550 File unavailable, {} not found\r\n", arg);
    return format("213 {}\r\n", fs::file_size(filePath));  // 213: thông tin file
}

// === handleMdtm: trả về thời gian sửa đổi cuối cùng của file ===
// Định dạng: YYYYMMDDHHmmss (14 chữ số, theo RFC 3659)
string CommandHandler::handleMdtm(Session& s, const string& arg) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";
    if (arg.empty()) return "501 Syntax error in parameters\r\n";

    string logical;
    fs::path filePath = resolvePath(s, arg, logical);
    if (filePath.empty() || !fs::exists(filePath))
        return format("550 File unavailable, {} not found\r\n", arg);

    // Lấy thời gian sửa đổi file → chuyển sang system_clock → time_t → tm
    auto ftime = fs::last_write_time(filePath);
    auto sctp = chr::clock_cast<chr::system_clock>(ftime);  // C++20: chuyển file_clock → system_clock
    auto tt = chr::system_clock::to_time_t(sctp);

    tm time; localtime_s(&time, &tt);

    return format("213 {:04}{:02}{:02}{:02}{:02}{:02}\r\n",
        time.tm_year + 1900, time.tm_mon + 1, time.tm_mday,
        time.tm_hour, time.tm_min, time.tm_sec);
}

// === handleStat: trạng thái Server hoặc thông tin file ===
// Không tham số → in trạng thái phiên hiện tại (user, dir, type, mode)
// Có tham số → in metadata file/thư mục (tên, ngày, loại, kích thước)
string CommandHandler::handleStat(Session& s, const string& arg) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";
    if (arg.empty()) {
        // Phản hồi multi-line: 211- ... 211 End
        return "211- FTP server status:\r\n"
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

        string name = filePath.filename().string();

        // Lấy thời gian sửa đổi
        auto ftime = fs::last_write_time(filePath);
        auto sctp = chr::clock_cast<chr::system_clock>(ftime);
        auto tt = chr::system_clock::to_time_t(sctp);
        tm time; localtime_s(&time, &tt);
        string date = format("{:04}{:02}{:02}{:02}{:02}{:02}", time.tm_year + 1900, time.tm_mon + 1, time.tm_mday, time.tm_hour, time.tm_min, time.tm_sec);

        string typeStr = fs::is_directory(filePath) ? "DIR" : "FILE";
        string sizeStr = fs::is_directory(filePath) ? "0" : std::to_string(fs::file_size(filePath));

        return format("211 {} {} {} {}\r\n", name, date, typeStr, sizeStr);
    }
}

// =====================================================================
// handleStor — UPLOAD FILE TỪ CLIENT LÊN SERVER (GHI ĐÈ)
//   1. Kiểm tra trạng thái: đăng nhập, có PORT/PASV, đường dẫn hợp lệ
//   2. Tạo DataChannel → bind → gửi phản hồi 150
//   3. Chạy receiveFile trên thread riêng → gửi 226/426 khi hoàn tất
//   Transfer chạy bất đồng bộ: hàm trả về "" (rỗng) ngay lập tức,
//   phản hồi 226/426 được gửi sau qua sendIntermediate().
// =====================================================================
string CommandHandler::handleStor(Session& s, const string& arg) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";
    if (arg.empty()) return "501 Syntax error in parameters\r\n";

    DataMode mode = s.getDataMode();
    if (mode == DataMode::NONE) return "425 Can't open data connection: send PORT or PASV before STOR\r\n";

    string logical;
    fs::path target = resolvePath(s, arg, logical);
    if (target.empty()) return "550 Invalid path\r\n";

    joinPreviousTransfer();

    auto dc = make_shared<DataChannel>(pickListenPort(s));
    if (!dc->start()) return "425 Can't open data connection\r\n";

    // Gửi phản hồi 150: sẵn sàng nhận dữ liệu
    sendIntermediate(appendPortIfNeeded(s, dc->getBoundPort(), "150 File status okay, opening data connection\r\n"));
    s.setActiveDataChannel(dc.get());

    bool isAscii = (s.getType() == "A");
    // Thread nhận file — shared_ptr đảm bảo DataChannel sống đến khi thread kết thúc
    transferThread = thread([this, &s, dc, target, isAscii]() {
        bool ok = dc->receiveFile(target.string(), false, isAscii);
        s.setActiveDataChannel(nullptr);
        dc->stop();
        // Kiểm tra cờ hủy (ABOR)
        if (s.isTransferAborted()) {
            this->sendIntermediate("426 Connection closed, transfer aborted\r\n");
            s.setTransferAborted(false);
        } else {
            this->sendIntermediate(ok ? "226 Transfer complete\r\n" : "426 Connection closed, transfer aborted\r\n");
        }
        s.resetDataMode();
        });

    return "";  // Không trả phản hồi ngay — sẽ gửi 226/426 bất đồng bộ
}

// =====================================================================
// handleRetr — TẢI FILE TỪ SERVER VỀ CLIENT
//   Tương tự STOR nhưng chiều ngược lại: Server gửi file cho Client.
//   Passive mode: sendFileAfterHandshake (chờ probe rồi gửi)
//   Active mode: sendFile trực tiếp đến IP:port Client
// =====================================================================
string CommandHandler::handleRetr(Session& s, const string& arg) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";
    if (arg.empty()) return "501 Syntax error in parameters\r\n";

    DataMode mode = s.getDataMode();
    if (mode == DataMode::NONE) return "425 Can't open data connection: send PORT or PASV before RETR\r\n";

    string logical;
    fs::path target = resolvePath(s, arg, logical);
    if (target.empty() || !fs::exists(target) || !fs::is_regular_file(target))
        return format("550 File unavailable, {} not found\r\n", arg);

    uintmax_t size = fs::file_size(target);

    joinPreviousTransfer();

    auto dc = make_shared<DataChannel>(pickListenPort(s));
    if (!dc->start()) return "425 Can't open data connection\r\n";

    // Phản hồi 150 kèm kích thước file
    sendIntermediate(appendPortIfNeeded(s, dc->getBoundPort(), format("150 File status okay, opening data connection ({} bytes)\r\n", size)));
    s.setActiveDataChannel(dc.get());

    string destIp = (mode == DataMode::ACTIVE) ? s.getActiveIp() : clientIp;
    unsigned short destPort = s.getActivePort();
    bool isAscii = (s.getType() == "A");

    transferThread = thread([this, &s, dc, target, mode, destIp, destPort, isAscii]() {
        // Passive → chờ probe rồi gửi; Active → gửi trực tiếp
        bool ok = (mode == DataMode::PASSIVE)
            ? dc->sendFileAfterHandshake(target.string(), isAscii)
            : dc->sendFile(target.string(), destIp, destPort, isAscii);
        s.setActiveDataChannel(nullptr);
        dc->stop();
        if (s.isTransferAborted()) {
            this->sendIntermediate("426 Connection closed, transfer aborted\r\n");
            s.setTransferAborted(false);
        } else {
            this->sendIntermediate(ok ? "226 Transfer complete\r\n" : "426 Connection closed, transfer aborted\r\n");
        }
        s.resetDataMode();
        });

    return "";
}

// === handleCwd: thay đổi thư mục làm việc ===
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

// === handleCdup: lên thư mục cha — tương đương CWD .. ===
string CommandHandler::handleCdup(Session& s) { return handleCwd(s, ".."); }

// === handleMkd: tạo thư mục mới ===
string CommandHandler::handleMkd(Session& s, const string& arg) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";
    if (arg.empty()) return "501 Syntax error in parameters\r\n";

    string newLogical;
    fs::path physical = resolvePath(s, arg, newLogical);
    if (physical.empty()) return "550 Invalid path\r\n";

    error_code ec;
    bool created = fs::create_directory(physical, ec);
    if (ec) return format("550 Can't create directory '{}' ({})\r\n", arg, ec.message());
    if (!created) return format("550 Directory '{}' already exists\r\n", arg);
    return format("257 \"{}\" created\r\n", newLogical);
}

// === handleRmd: xóa thư mục rỗng ===
// Không cho phép xóa thư mục gốc (SERVER_ROOT)
string CommandHandler::handleRmd(Session& s, const string& arg) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";
    if (arg.empty()) return "501 Syntax error in parameters\r\n";

    string logical;
    fs::path physical = resolvePath(s, arg, logical);
    // Chặn xóa thư mục gốc
    if (logical == "/" || physical == SERVER_ROOT)
        return "550 Cannot remove root directory\r\n";
    if (physical.empty() || !fs::exists(physical) || !fs::is_directory(physical))
        return format("550 {} : No such directory\r\n", arg);

    error_code ec;
    bool removed = fs::remove(physical, ec);  // fs::remove chỉ xóa thư mục rỗng
    if (ec || !removed) return format("550 Can't remove '{}', directory not empty\r\n", arg);
    return format("250 \"{}\" directory removed\r\n", arg);
}

// =====================================================================
// handleNlst — LIỆT KÊ THƯ MỤC CHỈ TÊN FILE (NAME LIST)
//   Giống LIST nhưng chỉ trả về tên file/thư mục (không có metadata).
//   Tạo file tạm → ghi danh sách vào → gửi file tạm qua DataChannel → xóa file tạm.
// =====================================================================
string CommandHandler::handleNlst(Session& s, const string& arg) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";

    DataMode mode = s.getDataMode();
    if (mode == DataMode::NONE) return "425 Can't open data connection\r\n";

    string logical;
    fs::path physical = resolvePath(s, arg, logical);
    if (physical.empty() || !fs::exists(physical) || !fs::is_directory(physical))
        return format("550 {} : No such directory\r\n", arg.empty() ? "." : arg);

    // Xây dựng danh sách tên file
    string body;
    for (auto& entry : fs::directory_iterator(physical))
        body += entry.path().filename().string() + "\r\n";

    // Tạo file tạm với tên unique (dùng timestamp)
    auto ms = chr::duration_cast<chr::milliseconds>(chr::system_clock::now().time_since_epoch()).count();
    string tempFileName = format(".temp_nlst_{}.tmp", ms);
    fs::path tempFile = SERVER_ROOT / tempFileName;
    int counter = 0;
    while (fs::exists(tempFile)) {
        counter++;
        tempFile = SERVER_ROOT / format(".temp_nlst_{}_{}.tmp", ms, counter);
    }

    ofstream ofs(tempFile);
    ofs << body;
    ofs.close();

    joinPreviousTransfer();

    auto dc = make_shared<DataChannel>(pickListenPort(s));
    if (!dc->start()) return "425 Can't open data connection\r\n";

    sendIntermediate(appendPortIfNeeded(s, dc->getBoundPort(), "150 Here comes the directory listing\r\n"));
    s.setActiveDataChannel(dc.get());

    string destIp = (mode == DataMode::ACTIVE) ? s.getActiveIp() : clientIp;
    unsigned short destPort = s.getActivePort();
    bool isAscii = (s.getType() == "A");

    transferThread = thread([this, &s, dc, tempFile, mode, destIp, destPort, isAscii]() {
        bool ok = (mode == DataMode::PASSIVE)
            ? dc->sendFileAfterHandshake(tempFile.string(), isAscii)
            : dc->sendFile(tempFile.string(), destIp, destPort, isAscii);

        s.setActiveDataChannel(nullptr);
        dc->stop();

        // Xóa file tạm
        error_code ec;
        fs::remove(tempFile, ec);

        if (s.isTransferAborted()) {
            this->sendIntermediate("426 Connection closed, transfer aborted\r\n");
            s.setTransferAborted(false);
        } else {
            this->sendIntermediate(ok ? "226 Transfer complete\r\n" : "426 Connection closed, transfer aborted\r\n");
        }
        s.resetDataMode();
    });

    return "";
}

// =====================================================================
// handleList — LIỆT KÊ THƯ MỤC CHI TIẾT
//   Trả về: tên, ngày sửa (YYYYMMDDHHmmss), loại (DIR/FILE), kích thước
//   cho mỗi entry. Ghi vào file tạm rồi gửi qua DataChannel.
// =====================================================================
string CommandHandler::handleList(Session& s, const string& arg) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";

    DataMode mode = s.getDataMode();
    if (mode == DataMode::NONE) return "425 Can't open data connection\r\n";

    string logical;
    fs::path physical = resolvePath(s, arg, logical);
    if (physical.empty() || !fs::exists(physical) || !fs::is_directory(physical))
        return format("550 {} : No such directory\r\n", arg.empty() ? "." : arg);

    // Xây dựng danh sách chi tiết
    string body;
    for (auto& entry : fs::directory_iterator(physical)) {
        bool isDir = entry.is_directory();
        uintmax_t size = isDir ? 0 : entry.file_size();

        auto ftime = fs::last_write_time(entry);
        auto sctp = chr::clock_cast<chr::system_clock>(ftime);
        auto tt = chr::system_clock::to_time_t(sctp);
        tm timeInfo;
        localtime_s(&timeInfo, &tt);

        string dateStr = format("{:04}{:02}{:02}{:02}{:02}{:02}",
            timeInfo.tm_year + 1900, timeInfo.tm_mon + 1, timeInfo.tm_mday,
            timeInfo.tm_hour, timeInfo.tm_min, timeInfo.tm_sec);

        body += format("{} {} {} {}\r\n", entry.path().filename().string(), dateStr, isDir ? "DIR" : "FILE", size);
    }

    // Tạo file tạm
    auto ms = chr::duration_cast<chr::milliseconds>(chr::system_clock::now().time_since_epoch()).count();
    string tempFileName = format(".temp_list_{}.tmp", ms);
    fs::path tempFile = SERVER_ROOT / tempFileName;
    int counter = 0;
    while (fs::exists(tempFile)) {
        counter++;
        tempFile = SERVER_ROOT / format(".temp_list_{}_{}.tmp", ms, counter);
    }

    ofstream ofs(tempFile);
    ofs << body;
    ofs.close();

    joinPreviousTransfer();

    auto dc = make_shared<DataChannel>(pickListenPort(s));
    if (!dc->start()) return "425 Can't open data connection\r\n";

    sendIntermediate(appendPortIfNeeded(s, dc->getBoundPort(), "150 Here comes the directory listing\r\n"));
    s.setActiveDataChannel(dc.get());

    string destIp = (mode == DataMode::ACTIVE) ? s.getActiveIp() : clientIp;
    unsigned short destPort = s.getActivePort();
    bool isAscii = (s.getType() == "A");

    transferThread = thread([this, &s, dc, tempFile, mode, destIp, destPort, isAscii]() {
        bool ok = (mode == DataMode::PASSIVE)
            ? dc->sendFileAfterHandshake(tempFile.string(), isAscii)
            : dc->sendFile(tempFile.string(), destIp, destPort, isAscii);

        s.setActiveDataChannel(nullptr);
        dc->stop();

        error_code ec;
        fs::remove(tempFile, ec);

        if (s.isTransferAborted()) {
            this->sendIntermediate("426 Connection closed, transfer aborted\r\n");
            s.setTransferAborted(false);
        } else {
            this->sendIntermediate(ok ? "226 Transfer complete\r\n" : "426 Connection closed, transfer aborted\r\n");
        }
        s.resetDataMode();
    });

    return "";
}

// =====================================================================
// handleStou — UPLOAD FILE VỚI TÊN TỰ ĐỘNG (STORE UNIQUE)
//   Tự động tạo tên file duy nhất: <basename>_<timestamp>.tmp
//   Nếu trùng → thêm counter. Client nhận tên file mới qua phản hồi 226.
// =====================================================================
string CommandHandler::handleStou(Session& s, const string& arg) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";
    if (arg.empty()) return "501 Syntax error in parameters\r\n";

    DataMode mode = s.getDataMode();
    if (mode == DataMode::NONE) return "425 Can't open data connection: send PORT or PASV before STOU\r\n";

    // Tạo tên file unique từ tên gốc + timestamp
    fs::path originalPath(arg);
    string baseName = originalPath.stem().string();
    if (baseName.empty()) baseName = "file";

    auto ms = chr::duration_cast<chr::milliseconds>(chr::system_clock::now().time_since_epoch()).count();
    string autoName = format("{}_{}.tmp", baseName, ms);

    string logical;
    fs::path target = resolvePath(s, autoName, logical);
    int counter = 0;
    while (!target.empty() && fs::exists(target)) {
        counter++;
        autoName = format("{}_{}_{}.tmp", baseName, ms, counter);
        target = resolvePath(s, autoName, logical);
    }
    if (target.empty()) return "550 Invalid path\r\n";

    joinPreviousTransfer();

    auto dc = make_shared<DataChannel>(pickListenPort(s));
    if (!dc->start()) return "425 Can't open data connection\r\n";

    // Phản hồi 150 kèm tên file đã tạo
    sendIntermediate(appendPortIfNeeded(s, dc->getBoundPort(), format("150 FILE: {}\r\n", autoName)));
    s.setActiveDataChannel(dc.get());

    bool isAscii = (s.getType() == "A");
    transferThread = thread([this, &s, dc, target, autoName, isAscii]() {
        bool ok = dc->receiveFile(target.string(), false, isAscii);
        s.setActiveDataChannel(nullptr);
        dc->stop();
        if (s.isTransferAborted()) {
            this->sendIntermediate("426 Connection closed, transfer aborted\r\n");
            s.setTransferAborted(false);
        } else {
            this->sendIntermediate(ok ? format("226 Transfer complete, stored as {}\r\n", autoName)
                : string("426 Connection closed, transfer aborted\r\n"));
        }
        s.resetDataMode();
        });

    return "";
}

// === handleAppe: upload nối thêm vào file hiện có (APPEND) ===
// Giống STOR nhưng dùng append=true thay vì trunc
string CommandHandler::handleAppe(Session& s, const string& arg) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";
    if (arg.empty()) return "501 Syntax error in parameters\r\n";

    DataMode mode = s.getDataMode();
    if (mode == DataMode::NONE) return "425 Can't open data connection: send PORT or PASV before APPE\r\n";


    string logical;
    fs::path target = resolvePath(s, arg, logical);
    if (target.empty()) return "550 Invalid path\r\n";

    joinPreviousTransfer();

    auto dc = make_shared<DataChannel>(pickListenPort(s));
    if (!dc->start()) return "425 Can't open data connection\r\n";

    sendIntermediate(appendPortIfNeeded(s, dc->getBoundPort(), "150 File status okay, opening data connection\r\n"));
    s.setActiveDataChannel(dc.get());

    bool isAscii = (s.getType() == "A");
    transferThread = thread([this, &s, dc, target, isAscii]() {
        bool ok = dc->receiveFile(target.string(), true, isAscii);  // append = true
        s.setActiveDataChannel(nullptr);
        dc->stop();
        if (s.isTransferAborted()) {
            this->sendIntermediate("426 Connection closed, transfer aborted\r\n");
            s.setTransferAborted(false);
        } else {
            this->sendIntermediate(ok ? "226 Transfer complete\r\n" : "426 Connection closed, transfer aborted\r\n");
        }
        s.resetDataMode();
        });

    return "";
}

// === handleDele: xóa file ===
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

// === handleRnfr: chỉ định file/thư mục cần đổi tên (RENAME FROM — bước 1/2) ===
// Lưu đường dẫn vào session, chờ lệnh RNTO tiếp theo
string CommandHandler::handleRnfr(Session& s, const string& arg) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";
    if (arg.empty()) return "501 Syntax error in parameters\r\n";

    string logical;
    fs::path target = resolvePath(s, arg, logical);
    // Không cho phép đổi tên thư mục gốc
    if (logical == "/" || target == SERVER_ROOT)
        return "550 Cannot rename root directory\r\n";
    if (target.empty() || !fs::exists(target))
        return format("550 {} : No such file or directory\r\n", arg);

    s.setRenameFrom(logical);  // Lưu đường dẫn gốc
    return "350 Requested file action pending further information\r\n";  // 350: chờ thêm thông tin (RNTO)
}

// === handleRnto: đổi tên file/thư mục (RENAME TO — bước 2/2) ===
// Phải gọi sau RNFR. Thực hiện fs::rename từ đường dẫn cũ sang mới.
string CommandHandler::handleRnto(Session& s, const string& arg) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";
    if (arg.empty()) return "501 Syntax error in parameters\r\n";
    if (s.getRenameFrom().empty()) return "503 Bad sequence of commands\r\n";  // Chưa có RNFR

    string oldLogical, newLogical;
    fs::path oldPath = resolvePath(s, s.getRenameFrom(), oldLogical);
    fs::path newPath = resolvePath(s, arg, newLogical);

    s.setRenameFrom("");  // Reset trạng thái RNFR

    if (oldPath.empty() || newPath.empty() || !fs::exists(oldPath)) return "550 Rename failed\r\n";

    error_code ec;
    fs::rename(oldPath, newPath, ec);
    if (ec) return format("550 Rename failed ({})\r\n", ec.message());
    return "250 Rename successful\r\n";
}

// === handleHash: tính SHA-256 hash của file ===
// Gọi computeFileSHA256() từ HashUtil
string CommandHandler::handleHash(Session& s, const string& arg) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";
    if (arg.empty()) return "501 Syntax error in parameters\r\n";

    string logical;
    fs::path filePath = resolvePath(s, arg, logical);
    if (filePath.empty() || !fs::exists(filePath) || !fs::is_regular_file(filePath))
        return format("550 File unavailable, {} not found\r\n", arg);

    joinPreviousTransfer();

    string hash = computeFileSHA256(filePath.string());
    if (hash.empty()) return format("550 Cannot compute hash of '{}'\r\n", arg);

    return format("213 SHA-256 {}\r\n", hash);
}

// =====================================================================
// handlePort — ACTIVE MODE: Client chỉ định IP:port
//   Tham số: "h1,h2,h3,h4,p1,p2" (6 số phân cách bằng dấu phẩy)
//   h1-h4 = 4 octet IP, port = p1*256 + p2
//   Ví dụ: "127,0,0,1,4,1" → IP=127.0.0.1, port=1025
// =====================================================================
string CommandHandler::handlePort(Session& s, const string& arg) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";

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

// =====================================================================
// handlePasv — PASSIVE MODE: Server mở port cho Client kết nối
//   Chọn port từ phạm vi [6000, 6999] (vòng tròn — quay lại 6000 khi hết)
//   Phản hồi: "227 Entering Passive Mode (h1,h2,h3,h4,p1,p2)"
//   p1 = port/256, p2 = port%256
// =====================================================================
string CommandHandler::handlePasv(Session& s) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";

    // Chọn port tuần tự từ 6000-6999, dùng atomic để an toàn đa luồng
    static atomic<unsigned short> nextPort{ 6000 };
    unsigned short port = nextPort.fetch_add(1);
    if (port > 6999) {
        nextPort.store(6000);
        port = 6000;
    }

    s.setPassiveMode(port);

    // Lấy IP thực tế của Server (IP mà Client đang kết nối đến)
    sockaddr_in localAddr = {};
    int len = sizeof(localAddr);
    getsockname(clientSocket, (sockaddr*)&localAddr, &len);
    char ipStr[INET_ADDRSTRLEN] = { 0 };
    inet_ntop(AF_INET, &localAddr.sin_addr, ipStr, INET_ADDRSTRLEN);
    string ip(ipStr);
    if (ip == "0.0.0.0" || ip.empty()) {
        ip = "127.0.0.1";  // Fallback nếu không xác định được IP
    }

    // Tách IP thành 4 octet
    vector<int> ipParts;
    stringstream ipss(ip);
    string seg;
    while (getline(ipss, seg, '.')) ipParts.push_back(std::stoi(seg));
    while (ipParts.size() < 4) ipParts.push_back(0);

    // Phản hồi 227 theo chuẩn FTP: (h1,h2,h3,h4,p1,p2)
    return format("227 Entering Passive Mode ({},{},{},{},{},{})\r\n",
        ipParts[0], ipParts[1], ipParts[2], ipParts[3], port / 256, port % 256);
}

// === handleAbor: hủy transfer đang diễn ra ===
// Gửi phản hồi 225 trước, sau đó gọi Session::abortActiveTransfer()
// để đóng DataChannel → thread transfer nhận lỗi và kết thúc
string CommandHandler::handleAbor(Session& s) {
    if (!s.getLoggedIn()) return "530 Not logged in\r\n";
    sendIntermediate("225 ABOR command successful\r\n");
    s.abortActiveTransfer();
    return "";
}

// =====================================================================
// handle — HÀM DISPATCH CHÍNH
//   Nhận tên lệnh + tham số → chuyển thành enum → switch-case gọi
//   hàm handler tương ứng → trả về phản hồi FTP (string)
//   Lệnh không nhận diện → trả về 500 Syntax error
// =====================================================================
string CommandHandler::handle(Session& s, const string& com, const string& arg) {
    FtpCommand fc = toFtpCommand(com);
    if (fc == FtpCommand::UNKNOWN)
        return format("500 Syntax error, command unrecognized: '{}'\r\n", com);

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
    case FtpCommand::LIST: return handleList(s, arg);
    case FtpCommand::NLST: return handleNlst(s, arg);
    case FtpCommand::STOU: return handleStou(s, arg);
    case FtpCommand::APPE: return handleAppe(s, arg);
    case FtpCommand::DELE: return handleDele(s, arg);
    case FtpCommand::RNFR: return handleRnfr(s, arg);
    case FtpCommand::RNTO: return handleRnto(s, arg);
    case FtpCommand::HASH: return handleHash(s, arg);
    case FtpCommand::PORT: return handlePort(s, arg);
    case FtpCommand::PASV: return handlePasv(s);
    case FtpCommand::ABOR: return handleAbor(s);
    default: return "502 Command not implemented\r\n";
    }
}