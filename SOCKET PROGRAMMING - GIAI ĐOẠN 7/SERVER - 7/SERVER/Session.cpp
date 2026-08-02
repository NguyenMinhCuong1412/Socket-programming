#include "Session.h"

Session::Session() {
	this->isLoggedIn = false;
	this->userName = "";
	this->currentDir = "/";
	this->dataType = "A";      
	this->transferMode = "S";  
	this->renameFrom = "";     
	this->dataMode = DataMode::NONE;
	this->activeIp = "";
	this->activePort = 0;  
	this->passivePort = 0; 
	this->activeDataChannel = nullptr;
}

Session::~Session() {
	// KHÔNG delete: activeDataChannel chỉ là con trỏ quan sát (xem giải thích trong setActiveDataChannel).
	// Đối tượng DataChannel thật sự được sở hữu bởi shared_ptr<DataChannel> nằm trong lambda của
	// transferThread (CommandHandler); shared_ptr đó tự giải phóng khi thread kết thúc.
	// CommandHandler::~CommandHandler() luôn join() transferThread TRƯỚC KHI Session bị hủy
	// (thứ tự hủy biến cục bộ ngược lại thứ tự khai báo trong ControlChannel::handleClient),
	// nên tới đây transfer chắc chắn đã xong và không còn tranh chấp với activeDataChannel.
	lock_guard<mutex> lock(this->dcMutex);
	this->activeDataChannel = nullptr;
}

void Session::setLoggedIn(bool logged) { this->isLoggedIn = logged; }
void Session::setUserName(string name) { this->userName = name; }
void Session::setDir(string dir) { this->currentDir = dir; }
void Session::setType(string type) { this->dataType = type; }
void Session::setMode(string mode) { this->transferMode = mode; }
void Session::setRenameFrom(string name) { this->renameFrom = name; }

void Session::setActiveMode(const string& ip, unsigned short port) {
	this->dataMode = DataMode::ACTIVE; 
	this->activeIp = ip; 
	this->activePort = port;
}

void Session::setPassiveMode(unsigned short port) {
	this->dataMode = DataMode::PASSIVE; 
	this->passivePort = port;
}

void Session::setActiveDataChannel(DataChannel* dc) {
	// LỖI CŨ (đã sửa): dòng "delete this->activeDataChannel" ở đây từng gây DOUBLE-FREE.
	// Nguyên nhân: handleStor/handleRetr/handleStou/handleAppe tạo `shared_ptr<DataChannel> dc`
	// rồi gọi setActiveDataChannel(dc.get()) — Session chỉ nhận một con trỏ thô mượn tạm.
	// Khi transfer xong, code gọi setActiveDataChannel(nullptr); nếu hàm này delete con trỏ đó,
	// thì ngay sau đó, khi lambda của transferThread kết thúc, shared_ptr `dc` cũng tự delete
	// CHÍNH đối tượng đó lần thứ hai -> heap corruption / crash không ổn định, rất khó tái hiện.
	// Sửa: hàm này chỉ đơn thuần lưu/xóa con trỏ quan sát để ABOR có thể gọi stop(), không bao giờ delete.
	lock_guard<mutex> lock(this->dcMutex);
	this->activeDataChannel = dc;
}

bool Session::abortActiveTransfer() {
	lock_guard<mutex> lock(this->dcMutex);
	if (this->activeDataChannel != nullptr) {
		this->activeDataChannel->stop();   // closesocket() -> recvfrom/sendto đang block ở thread phụ báo lỗi
		return true;
	}
	return false;
}