// ======================================================================
// Session.h — PHIÊN LÀM VIỆC (SESSION) CỦA MỘT CLIENT TRÊN SERVER
//    Lưu trữ trạng thái riêng của từng Client kết nối: đăng nhập,
//    thư mục hiện tại, chế độ truyền (ASCII/Binary), chế độ dữ liệu
//    (Active/Passive), thông tin đổi tên file (RNFR/RNTO), và con trỏ
//    đến DataChannel đang hoạt động (để hỗ trợ lệnh ABOR hủy transfer).
//    Mỗi thread xử lý client có một đối tượng Session riêng.
// ======================================================================
#pragma once
#include "lib.h"
#include "DataChannel.h"

class Session {
private:
	bool isLoggedIn;             // true nếu Client đã xác thực thành công (USER + PASS)
	string userName;             // Tên người dùng (từ lệnh USER)
	string currentDir;           // Thư mục làm việc hiện tại (logical path, vd: "/folderTest")
	string dataType;             // Kiểu dữ liệu truyền: "A" (ASCII) hoặc "I" (Image/Binary)
	string transferMode;         // Chế độ truyền: "S" (Stream) — chỉ hỗ trợ Stream mode
	string renameFrom;           // Đường dẫn gốc cho thao tác đổi tên (lệnh RNFR — bước 1/2)
	DataMode dataMode;           // Chế độ kết nối dữ liệu: ACTIVE hoặc PASSIVE
	string activeIp;             // IP mà Client chỉ định trong lệnh PORT (Active mode)
	unsigned short activePort;   // Port mà Client chỉ định trong lệnh PORT (Active mode)
	unsigned short passivePort;  // Port Server đã mở cho Passive mode (lệnh PASV)
	bool isAborted;              // Cờ hủy transfer (lệnh ABOR)
	mutex dcMutex;               // Mutex bảo vệ activeDataChannel — tránh race condition với thread transfer
	DataChannel* activeDataChannel; // Con trỏ đến DataChannel đang truyền — dùng cho ABOR (stop DataChannel)
public:
	Session();
	~Session();

	// === Getter — truy vấn trạng thái phiên ===
	bool getLoggedIn() const { return this->isLoggedIn; }
	string getUserName() const { return this->userName; }
	string getDir() const { return this->currentDir; }
	string getType() const { return this->dataType; }
	string getMode() const { return this->transferMode; }
	string getRenameFrom() const { return this->renameFrom; }
	DataMode getDataMode() const { return this->dataMode; }
	string getActiveIp() const { return this->activeIp; }
	unsigned short getActivePort() const { return this->activePort; }
	unsigned short getPassivePort() const { return this->passivePort; }

	// === Setter — cập nhật trạng thái phiên ===
	void setLoggedIn(bool);
	void setUserName(string);
	void setDir(string);
	void setType(string);
	void setMode(string);
	void setRenameFrom(string);
	void setActiveMode(const string&, unsigned short);  // Đặt Active mode: lưu IP và port Client
	void setPassiveMode(unsigned short);                 // Đặt Passive mode: lưu port Server đã mở
	void resetDataMode();                                // Reset về trạng thái ban đầu (ACTIVE, không có thông tin port)
	void setActiveDataChannel(DataChannel*);             // Đăng ký DataChannel đang hoạt động (bảo vệ bằng mutex)
	bool abortActiveTransfer();                          // Hủy transfer: đặt cờ isAborted và stop DataChannel
	bool isTransferAborted();                            // Kiểm tra cờ hủy transfer
	void setTransferAborted(bool);                       // Đặt/xóa cờ hủy transfer
};