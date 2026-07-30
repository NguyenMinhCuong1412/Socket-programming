#pragma once
#include "lib.h"
#include "DataChannel.h"

//Phiên làm việc - mỗi Client có 1 đối tượng riêng (tạo trong thread riêng của Client đó)
class Session {
private:
	bool isLoggedIn;     //Đăng nhập
	string userName;     //Tên
	string currentDir;   //Thư mục làm việc hiện tại của Client trên Server
	string dataType;     //Định dạng dữ liệu - A = ASCII, I = IMAGE/BINARY
	string transferMode; //Cách truyền dữ liệu - S = STREAM, B = BLOCK, C = COMPRESSED
	string renameFrom;   //Tên (logical) đang chờ RNTO hoàn tất; rỗng = không có RNFR đang chờ

	DataMode dataMode;

	string activeIp;            //ACTIVE: IP client gửi qua lệnh PORT
	unsigned short activePort;  //ACTIVE: port client gửi qua lệnh PORT

	unsigned short passivePort; //PASSIVE: port server tự chọn, đã thông báo qua PASV

	mutex dcMutex;              //Mutex bảo vệ activeDataChannel (mutable để dùng được trong hàm const)
	DataChannel* activeDataChannel;
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