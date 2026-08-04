#pragma once
#include "lib.h"
#include "DataChannel.h"

//Phiên làm việc - mỗi Client có phiên riêng độc lập
class Session {
private:
	bool isLoggedIn;                //Đăng nhập
	string userName;                //Tên
	string currentDir;              //Thư mục làm việc hiện tại của Client trên Server
	string dataType;                //Định dạng dữ liệu - A = ASCII, I = IMAGE/BINARY
	string transferMode;            //Cách truyền dữ liệu - S = STREAM, B = BLOCK, C = COMPRESSED
	string renameFrom;              //Tên (logical) đang chờ RNTO hoàn tất; rỗng = không có RNFR đang chờ
	DataMode dataMode;              //Chế độ truyền dữ liệu - ACTIVE = Client tự chọn port, PASSIVE = Server tự chọn port
	string activeIp;                //ACTIVE: IP của Client gửi qua lệnh PORT
	unsigned short activePort;      //ACTIVE: Port của Client gửi qua lệnh PORT
	unsigned short passivePort;     //PASSIVE: Port của Server tự chọn
	mutex dcMutex;                  //Mutex bảo vệ activeDataChannel, tránh crash giữa 2 luồng chính và phụ/crash giữa 2 luồng phụ cùng Session
	DataChannel* activeDataChannel; //Con trỏ QUAN SÁT (không sở hữu) tới DataChannel đang chạy của Client, chỉ dùng để ABOR gọi stop(); vòng đời thật thuộc shared_ptr bên CommandHandler
public:
	Session();
	~Session(); 

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

	void setLoggedIn(bool);
	void setUserName(string);
	void setDir(string);
	void setType(string);
	void setMode(string);
	void setRenameFrom(string);
	void setActiveMode(const string&, unsigned short);
	void setPassiveMode(unsigned short);
	void setActiveDataChannel(DataChannel*);
	bool abortActiveTransfer();
};