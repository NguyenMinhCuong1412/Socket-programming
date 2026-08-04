#pragma once
#include "lib.h"

//Khởi tạo kênh điều khiển Client-TCP
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

	//Lệnh vừa gửi gần nhất - để thread nền biết cần làm STOR/RETR/... gì khi thấy "150"
	mutex pendingMutex;
	string pendingCmdWord;
	string pendingArg;

	//Thread nền: liên tục nhận phản hồi từ server + tự thực hiện data-transfer (không ảnh hưởng thread đọc bàn phím, cho phép gõ ABOR)
	thread receiverThread;
	atomic<bool> keepRunning{ true };

	bool parsePortArgLocal(const string& arg, unsigned short& outPort);
	bool parsePasvReply(const string& reply, unsigned short& outPort);
	bool parseEmbeddedPort(const string& reply, unsigned short& outPort); //Đọc " PORT=<n>" nhúng trong reply "150"

	//Tự động thực hiện "PORT" với 1 cổng NGẪU NHIÊN do OS cấp khi user gõ RETR mà chưa chọn PORT/PASV trước đó
	bool autoNegotiateActivePort();

	void receiverLoop();
	void doDataTransfer(const string& cmdWord, const string& filename);
public:
	ControlChannel(unsigned short, string);
	~ControlChannel();

	bool start(); //Tạo socket + định danh địa chỉ + Bind + Connect
	void run();   //Bắt đầu thread nền, rồi vòng lặp đọc bàn phím/gửi lệnh
	void stop();  //Đóng socket
};
