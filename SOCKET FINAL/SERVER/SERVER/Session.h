#pragma once
#include "lib.h"

//Phiên làm việc
class Session {
private:
	bool isLoggedIn;
	string userName;
	string currentDir;
	string dataType;     // "A" = ASCII, "I" = IMAGE/BINARY
	string transferMode; // "S" = Stream, "B" = Block, "C" = Compressed

    SOCKET dataSocket;          //Socket UDP dùng chung cho cả STOR và RETR (Giai đoạn 3)
    sockaddr_in clientDataAddr; //Địa chỉ UDP của Client (IP lấy từ TCP + DATA_PORT cố định)
public:
	Session();

    bool getLogged() const { return this->isLoggedIn; }
    string getName() const { return this->userName; }
    string getDir() const { return this->currentDir; }
    string getType() const { return this->dataType; }
    string getMode() const { return this->transferMode; }

    SOCKET getDataSocket() const { return this->dataSocket; }
    sockaddr_in getClientDataAddr() const { return this->clientDataAddr; }

    void setLogged(bool);
    void setName(const string&);
    void setDir(const string&);
    void setType(const string&);
    void setMode(const string&);

    void setDataSocket(SOCKET);
    void setClientDataAddr(const sockaddr_in&);

    ~Session() = default;
};