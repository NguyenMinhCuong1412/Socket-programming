// ======================================================================
// Session.cpp — CÀI ĐẶT PHIÊN LÀM VIỆC CỦA CLIENT TRÊN SERVER
//    Triển khai constructor, các setter, và cơ chế hủy transfer (ABOR).
//    Các thao tác trên activeDataChannel đều bảo vệ bằng dcMutex
//    vì DataChannel có thể bị truy cập từ thread xử lý lệnh (ABOR)
//    và thread truyền dữ liệu đồng thời.
// ======================================================================
#include "Session.h"

// Constructor: khởi tạo phiên với giá trị mặc định
Session::Session() {
	this->isLoggedIn = false;
	this->userName = "";
	this->currentDir = "/";       // Thư mục gốc
	this->dataType = "A";         // Mặc định ASCII mode (theo chuẩn FTP)
	this->transferMode = "S";     // Mặc định Stream mode (FTP chỉ bắt buộc hỗ trợ Stream)
	this->renameFrom = "";
	this->dataMode = DataMode::ACTIVE;  // Mặc định Active mode
	this->activeIp = "";
	this->activePort = 0;
	this->passivePort = 0;
	this->activeDataChannel = nullptr;
	this->isAborted = false;
}

// Destructor: xóa tham chiếu đến DataChannel (bảo vệ bằng mutex)
Session::~Session() {
	lock_guard<mutex> lock(this->dcMutex);
	this->activeDataChannel = nullptr;
}

// === Các setter đơn giản ===
void Session::setLoggedIn(bool logged) { this->isLoggedIn = logged; }
void Session::setUserName(string name) { this->userName = name; }
void Session::setDir(string dir) { this->currentDir = dir; }
void Session::setType(string type) { this->dataType = type; }
void Session::setMode(string mode) { this->transferMode = mode; }
void Session::setRenameFrom(string name) { this->renameFrom = name; }

// Đặt Active mode: lưu IP và port mà Client chỉ định qua lệnh PORT
void Session::setActiveMode(const string& ip, unsigned short port) {
	this->dataMode = DataMode::ACTIVE;
	this->activeIp = ip;
	this->activePort = port;
}

// Đặt Passive mode: lưu port mà Server đã mở cho Client kết nối
void Session::setPassiveMode(unsigned short port) {
	this->dataMode = DataMode::PASSIVE;
	this->passivePort = port;
}

// Reset chế độ dữ liệu về mặc định — gọi sau mỗi lần transfer hoàn tất
void Session::resetDataMode() {
	this->dataMode = DataMode::ACTIVE;
	this->activeIp = "";
	this->activePort = 0;
	this->passivePort = 0;
}

// Đăng ký DataChannel đang hoạt động — bảo vệ bằng mutex vì có thể bị gọi
// từ thread transfer (đăng ký/hủy đăng ký) và thread lệnh (ABOR) đồng thời
void Session::setActiveDataChannel(DataChannel* dc) {
	lock_guard<mutex> lock(this->dcMutex);
	this->activeDataChannel = dc;
}

// Hủy transfer đang diễn ra (lệnh ABOR):
// 1. Đặt cờ isAborted = true
// 2. Nếu có DataChannel đang hoạt động → gọi stop() để đóng socket UDP
//    → các hàm rdtSend/rdtReceive đang chờ sẽ nhận lỗi và thoát
// Trả về true nếu có DataChannel đang hoạt động để hủy
bool Session::abortActiveTransfer() {
	lock_guard<mutex> lock(this->dcMutex);
	this->isAborted = true;
	if (this->activeDataChannel != nullptr) {
		this->activeDataChannel->stop();
		return true;
	}
	return false;
}

// Kiểm tra cờ hủy transfer — thread transfer kiểm tra cờ này sau khi kết thúc
bool Session::isTransferAborted() {
	lock_guard<mutex> lock(this->dcMutex);
	return this->isAborted;
}

// Đặt/xóa cờ hủy transfer — thread transfer reset cờ sau khi đã xử lý
void Session::setTransferAborted(bool ab) {
	lock_guard<mutex> lock(this->dcMutex);
	this->isAborted = ab;
}