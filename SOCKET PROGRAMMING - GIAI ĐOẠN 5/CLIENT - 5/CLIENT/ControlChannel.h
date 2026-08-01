#pragma once
#include "lib.h"

class ControlChannel {
private:
	unsigned short serverTcpPort; //Port TCP của Server
	string serverIp;              //IP của Server
	SOCKET tcpSocket;             //socket phục vụ kênh điều khiển - Client

	// Trạng thái PORT/PASV - atomic vì được đọc/ghi từ CẢ 2 thread (bàn phím + nhận nền)
	atomic<ClientDataMode> dataMode{ ClientDataMode::NONE };
	atomic<unsigned short> myActivePort{ 0 };   // ACTIVE: port client tự bind (đã báo server qua PORT)
	atomic<unsigned short> serverPasvPort{ 0 }; // PASSIVE: port server đã chọn (đọc từ phản hồi 227)

	// Lệnh vừa gửi gần nhất - để thread nền biết cần làm STOR/RETR/... gì khi thấy "150"
	std::mutex pendingMutex;
	string pendingCmdWord;
	string pendingArg;

	// Thread nền: liên tục nhận phản hồi từ server + tự thực hiện data-transfer (BLOCKING trong
	// thread này, KHÔNG ảnh hưởng thread đọc bàn phím) -> cho phép gõ ABOR giữa lúc transfer đang chạy.
	std::thread receiverThread;
	std::atomic<bool> keepRunning{ true };

	bool parsePortArgLocal(const string& arg, unsigned short& outPort);
	bool parsePasvReply(const string& reply, unsigned short& outPort);

	void receiverLoop();
	void doDataTransfer(const string& cmdWord, const string& filename);
public:
	ControlChannel(unsigned short, string);
	~ControlChannel();

	bool start(); //Tạo socket + định danh địa chỉ + Bind + Listen
	void run();   //Bắt đầu thread nền, rồi vòng lặp đọc bàn phím/gửi lệnh (không bao giờ block)
	void stop();  //Đóng socket
};