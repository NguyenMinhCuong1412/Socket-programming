#pragma once
#include "lib.h"

//Khởi tạo kênh điều khiển Client-TCP
class DataChannel;
class ControlChannel {
private:
	unsigned short serverTcpPort; //Port TCP của Server
	string serverIp;              //IP của Server
	SOCKET tcpSocket;             //socket phục vụ kênh điều khiển Client-TCP

	//Trạng thái PORT/PASV - atomic vì được đọc/ghi từ CẢ 2 thread (bàn phím + nhận nền)
	atomic<DataMode> dataMode{ DataMode::NONE };
	atomic<unsigned short> myActivePort{ 0 };    //ACTIVE: port client tự bind (đã báo server qua PORT)
	atomic<unsigned short> serverPasvPort{ 0 };  //PASSIVE: port server đã chọn (đọc từ phản hồi 227)
	atomic<unsigned short> serverUploadPort{ 0 };//ACTIVE/NONE + STOR/APPE/STOU: cổng NGẪU NHIÊN server vừa bind, đọc được từ " PORT=<n>" nhúng trong reply "150"
	atomic<bool> isAsciiMode{ true };            //Trạng thái truyền file: true = ASCII, false = BINARY
	atomic<DataChannel*> activeDataChannel{ nullptr }; // Con trỏ tới DataChannel đang chạy để stop khi bị ABOR

	//Lệnh vừa gửi gần nhất - để thread nền biết cần làm STOR/RETR/... gì khi thấy "150"
	mutex pendingMutex;
	string pendingCmdWord;
	string pendingArg;
	string currentDir;  //Thư mục ảo hiện tại trong client_root (bắt đầu "/")

	//Giải quyết đường dẫn: map filename -> đường dẫn thật trong client_root
	fs::path resolvePath(const string& arg);

	//Thread nền: liên tục nhận phản hồi từ server + tự thực hiện data-transfer (không ảnh hưởng thread đọc bàn phím, cho phép gõ ABOR)
	thread receiverThread;
	atomic<bool> keepRunning{ true };

	//Đồng bộ prompt "ftp>": sau khi gửi lệnh, thread bàn phím phải CHỜ đến khi
	//receiverLoop() nhận được phản hồi tương ứng rồi mới được in "ftp> " tiếp theo,
	//tránh in prompt 2 lần cho cùng 1 lệnh (prompt in trước, phản hồi in sau)
	mutex replyMutex;
	condition_variable replyCv;
	bool awaitingReply = false;

	bool parsePortArgLocal(const string& arg, unsigned short& outPort);
	bool parsePasvReply(const string& reply, unsigned short& outPort);
	bool parseEmbeddedPort(const string& reply, unsigned short& outPort); //Đọc " PORT=<n>" nhúng trong reply "150"



	void receiverLoop();
	void doDataTransfer(const string& cmdWord, const string& filename, uintmax_t totalSize = 0);
public:
	ControlChannel(unsigned short, string);
	~ControlChannel();

	bool start(); //Tạo socket + định danh địa chỉ + Bind + Connect
	void run();   //Bắt đầu thread nền, rồi vòng lặp đọc bàn phím/gửi lệnh
	void stop();  //Đóng socket
};
