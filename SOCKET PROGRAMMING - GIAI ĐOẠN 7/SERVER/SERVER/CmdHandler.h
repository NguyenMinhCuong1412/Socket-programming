// ======================================================================
// CmdHandler.h — BỘ XỬ LÝ LỆNH FTP CỦA SERVER
//    Khai báo hàm parseCmd (tách lệnh/tham số), toFtpCommand (chuỗi → enum),
//    và class CommandHandler chứa tất cả hàm xử lý cho từng lệnh FTP.
//    Mỗi thread Client có một CommandHandler riêng.
//    Hỗ trợ 27 lệnh FTP: USER, PASS, PWD, NOOP, QUIT, HELP, TYPE, MODE,
//    SIZE, STAT, MDTM, STOR, RETR, CWD, CDUP, MKD, RMD, LIST, NLST,
//    STOU, APPE, DELE, RNFR, RNTO, HASH, PORT, PASV, ABOR.
// ======================================================================
#pragma once
#include "lib.h"
#include "Session.h"

// Tách dòng lệnh thô (vd: "RETR file.txt\r\n") thành tên lệnh (cmd) và tham số (arg)
// cmd được chuyển thành chữ hoa, arg giữ nguyên
void parseCmd(const string&, string&, string&);

// Chuyển tên lệnh (string) thành giá trị enum FtpCommand để dùng trong switch-case
FtpCommand toFtpCommand(const string& command);

class CommandHandler {
private:
    SOCKET clientSocket;    // Socket TCP của Client — dùng để gửi phản hồi trung gian (150, 226...)
    string clientIp;        // IP của Client — dùng cho Passive mode (sendFile cần biết IP đích)
    thread transferThread;  // Thread truyền dữ liệu (gửi/nhận file chạy bất đồng bộ)

    // Chờ thread transfer trước đó hoàn tất trước khi bắt đầu transfer mới
    void joinPreviousTransfer();
    // Gửi phản hồi trung gian (150, 226, 426...) trên kênh điều khiển TCP
    void sendIntermediate(const string& msg);
    // Phân giải đường dẫn: logical path (vd: "/dir/file") → physical path (vd: "server_root/dir/file")
    // Kiểm tra path traversal: đảm bảo không thoát khỏi SERVER_ROOT
    fs::path resolvePath(const Session& s, const string& arg, string& outLogical);

    // === Hàm xử lý cho từng lệnh FTP (RFC 959) ===
    string handleUser(Session&, const string&);  // USER: nhận tên người dùng
    string handlePass(Session&, const string&);  // PASS: nhận mật khẩu (chấp nhận mọi mật khẩu)
    string handlePwd(Session&);                  // PWD: in thư mục hiện tại
    string handleNoop();                         // NOOP: không làm gì (keep-alive)
    string handleQuit();                         // QUIT: đóng kết nối
    string handleHelp(const string&);            // HELP: liệt kê lệnh hỗ trợ hoặc mô tả lệnh cụ thể
    string handleType(Session&, const string&);  // TYPE: đặt chế độ truyền (A=ASCII, I=Binary)
    string handleMode(Session&, const string&);  // MODE: đặt chế độ truyền (S=Stream)
    string handleSize(Session&, const string&);  // SIZE: trả về kích thước file
    string handleStat(Session&, const string&);  // STAT: trạng thái Server hoặc thông tin file
    string handleMdtm(Session&, const string&);  // MDTM: thời gian sửa đổi file
    string handleStor(Session&, const string&);  // STOR: nhận file upload từ Client (ghi đè)
    string handleRetr(Session&, const string&);  // RETR: gửi file cho Client tải về
    string handleCwd(Session&, const string&);   // CWD: thay đổi thư mục làm việc
    string handleCdup(Session&);                 // CDUP: lên thư mục cha (= CWD ..)
    string handleMkd(Session&, const string&);   // MKD: tạo thư mục mới
    string handleRmd(Session&, const string&);   // RMD: xóa thư mục rỗng
    string handleList(Session&, const string&);  // LIST: liệt kê thư mục chi tiết (tên, ngày, loại, kích thước)
    string handleNlst(Session&, const string&);  // NLST: liệt kê thư mục chỉ tên file
    string handleStou(Session&, const string&);  // STOU: upload với tên file tự động (tránh trùng)
    string handleAppe(Session&, const string&);  // APPE: upload nối thêm vào file hiện có
    string handleDele(Session&, const string&);  // DELE: xóa file
    string handleRnfr(Session&, const string&);  // RNFR: chỉ định file cần đổi tên (bước 1/2)
    string handleRnto(Session&, const string&);  // RNTO: đổi tên file (bước 2/2, sau RNFR)
    string handleHash(Session&, const string&);  // HASH: tính SHA-256 hash của file
    string handlePort(Session&, const string&);  // PORT: Client chỉ định IP:port cho Active mode
    string handlePasv(Session&);                 // PASV: Server mở port cho Passive mode
    string handleAbor(Session&);                 // ABOR: hủy transfer đang diễn ra

    // Chọn port bind cho DataChannel tùy chế độ (Passive → dùng passivePort, Active → 0)
    unsigned short pickListenPort(Session&);
    // Gắn thêm "PORT=xxx" vào phản hồi 150 nếu đang ở Active mode (Client cần biết port Server bind)
    string appendPortIfNeeded(Session&, unsigned short, const string&);
public:
    CommandHandler();
    ~CommandHandler();

    void setControlSocket(SOCKET s) { this->clientSocket = s; }
    void setClientIp(const string& ip) { this->clientIp = ip; }

    // Hàm dispatch chính: nhận tên lệnh + tham số → gọi handler tương ứng → trả về phản hồi FTP
    string handle(Session&, const string&, const string&);
};