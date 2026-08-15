// ======================================================================
// ControlChannel.h — KÊNH ĐIỀU KHIỂN (TCP) CỦA CLIENT
//    Quản lý kết nối TCP đến Server, gửi lệnh FTP, nhận phản hồi,
//    và điều phối kênh dữ liệu (DataChannel) cho các lệnh truyền file.
//    Sử dụng mô hình đa luồng: thread chính đọc lệnh từ người dùng,
//    thread phụ (receiverThread) lắng nghe phản hồi từ Server liên tục.
//    Hỗ trợ cả Active mode (PORT) và Passive mode (PASV).
// ======================================================================
#pragma once
#include "lib.h"

class DataChannel;  // Forward declaration — tránh include vòng
class ControlChannel {
private:
	unsigned short serverTcpPort;  // Cổng TCP của Server (mặc định CONTROL_PORT = 8080)
	string serverIp;               // Địa chỉ IP của Server (vd: "127.0.0.1")
	SOCKET tcpSocket;              // Socket TCP kết nối đến Server

	// === Trạng thái chế độ truyền dữ liệu ===
	atomic<DataMode> dataMode{ DataMode::NONE };        // Chế độ hiện tại: NONE/ACTIVE/PASSIVE
	atomic<unsigned short> myActivePort{ 0 };            // Port Active mode do Client chỉ định (lệnh PORT)
	atomic<unsigned short> serverPasvPort{ 0 };          // Port Passive mode Server đã mở (từ phản hồi 227)
	atomic<unsigned short> serverUploadPort{ 0 };        // Port Server bind cho việc upload (từ "PORT=" trong phản hồi 150)
	atomic<bool> isAsciiMode{ true };                    // true = ASCII mode, false = Binary mode (lệnh TYPE)
	atomic<DataChannel*> activeDataChannel{ nullptr };   // Con trỏ đến DataChannel đang hoạt động — dùng cho lệnh ABOR

	// === Lệnh đang chờ xử lý (chia sẻ giữa thread nhập lệnh và thread nhận phản hồi) ===
	mutex pendingMutex;        // Bảo vệ pendingCmdWord và pendingArg khỏi race condition
	string pendingCmdWord;     // Từ lệnh đang chờ (vd: "RETR", "LIST", "STOR")
	string pendingArg;         // Tham số lệnh đang chờ (vd: tên file)
	string currentDir;         // Thư mục làm việc hiện tại trên Client (logical path)

	// Phân giải đường dẫn tương đối thành đường dẫn tuyệt đối trong CLIENT_ROOT
	fs::path resolvePath(const string& arg);

	// === Thread nhận phản hồi từ Server ===
	thread receiverThread;             // Thread chạy receiverLoop() liên tục
	atomic<bool> keepRunning{ true };  // Cờ dừng: false → thoát vòng lặp nhận và vòng lặp lệnh

	// === Đồng bộ hóa giữa thread nhập lệnh và thread nhận phản hồi ===
	mutex replyMutex;                  // Bảo vệ biến awaitingReply
	condition_variable replyCv;        // Dùng để thread lệnh chờ (wait) cho đến khi nhận đủ phản hồi
	bool awaitingReply = false;        // true = đang chờ phản hồi từ Server cho lệnh vừa gửi

	// === Hàm phân tích phản hồi từ Server ===
	// Phân tích tham số lệnh PORT (h1,h2,h3,h4,p1,p2) → tính port = p1*256 + p2
	bool parsePortArgLocal(const string& arg, unsigned short& outPort);
	// Phân tích phản hồi 227 Passive mode: "227 Entering Passive Mode (h1,h2,h3,h4,p1,p2)" → lấy port
	bool parsePasvReply(const string& reply, unsigned short& outPort);
	// Phân tích chuỗi "PORT=xxxxx" nhúng trong phản hồi 150 → lấy port Server dùng cho upload
	bool parseEmbeddedPort(const string& reply, unsigned short& outPort);



	// Vòng lặp nhận phản hồi từ Server — chạy trên thread riêng
	void receiverLoop();
	// Thực hiện truyền dữ liệu: tạo DataChannel, gửi/nhận file tùy lệnh (RETR/STOR/LIST/NLST...)
	void doDataTransfer(const string& cmdWord, const string& filename, uintmax_t totalSize = 0);
public:
	ControlChannel(unsigned short, string);  // Khởi tạo với port và IP của Server
	~ControlChannel();                        // Destructor: dừng thread nhận

	bool start();  // Tạo socket TCP, connect đến Server
	void run();    // Vòng lặp chính: đọc lệnh từ stdin → gửi → chờ phản hồi
	void stop();   // Đóng kết nối TCP, dừng thread nhận
};
