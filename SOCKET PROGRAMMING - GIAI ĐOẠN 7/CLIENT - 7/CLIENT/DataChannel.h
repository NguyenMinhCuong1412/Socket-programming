#pragma once
#include "lib.h"
#include "RdtPacket.h"

//Khởi tạo kênh dữ liệu - UDP
class DataChannel {
private:
	unsigned short udpPort;   //Port để phục vụ kênh dữ liệu
	atomic<SOCKET> udpSocket; //Socket phục vụ kênh dữ liệu của Server-UDP, đảm bảo stop() có thể đóng socket an toàn

	// ====== RDT — Reliable Data Transfer (Giai đoạn 6) ======

	// Gửi 1 buffer dữ liệu qua RDT (Stop-and-Wait ARQ)
	// Tự chia thành các chunk RDT_MAX_PAYLOAD, gửi từng gói DATA + chờ ACK,
	// cuối cùng gửi FIN để báo kết thúc.
	// Return: true nếu toàn bộ dữ liệu đã được ACK thành công
	bool rdtSend(SOCKET s, const char* data, int len, const sockaddr_in& dest);

	// Nhận toàn bộ dữ liệu qua RDT (Stop-and-Wait ARQ)
	// Vòng lặp nhận DATA → ACK → cho đến khi nhận FIN.
	// outData: buffer chứa toàn bộ payload đã nhận
	// senderAddr: địa chỉ bên gửi (output — để caller biết ai gửi)
	// Return: tổng số byte dữ liệu nhận được, -1 nếu lỗi
	int rdtReceive(SOCKET s, std::vector<char>& outData, sockaddr_in& senderAddr);

public:
	DataChannel(unsigned short);
	~DataChannel() = default;

	bool start();
	unsigned short getBoundPort() const; //Trả về cổng UDP thực tế đã bind (dùng khi bind với port=0 để OS tự cấp phát ngẫu nhiên)
	bool receiveFile(const string&, bool = false);
	bool sendFile(const string&, const string&, unsigned short);

	//PASSIVE + RETR: server chưa biết địa chỉ Client, chờ nhận 1 gói "probe" nhỏ để học địa chỉ, rồi mới gửi file tới đúng địa chỉ đó
	bool sendFileAfterHandshake(const string&);

	//Phía Client khi RETR trong chế độ PASSIVE: gửi 1 gói tin nhỏ để server học địa chỉ IP:port
	bool sendProbe(const string&, unsigned short);

	void stop();
};