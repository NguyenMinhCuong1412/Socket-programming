#include "DataChannel.h"

DataChannel::DataChannel(unsigned short port) {
	this->udpPort = port;
	this->udpSocket.store(INVALID_SOCKET);
}

unsigned short DataChannel::getBoundPort() const {
	SOCKET s = udpSocket.load();
	if (s == INVALID_SOCKET) return 0;
	sockaddr_in local;
	int len = sizeof(local);
	if (getsockname(s, (sockaddr*)&local, &len) == SOCKET_ERROR) return 0;
	return ntohs(local.sin_port);
}

bool DataChannel::start() {
	//Tạo UDP-socket
	SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (s == INVALID_SOCKET) {
		cerr << format("425 Can't open data connection, cannot create socket (WSA error: {})", WSAGetLastError()) << endl;
		return false;
	}

	//Định danh địa chỉ Server-UDP
	sockaddr_in serverAddrUdp;
	serverAddrUdp.sin_family = AF_INET;
	serverAddrUdp.sin_addr.s_addr = INADDR_ANY;
	serverAddrUdp.sin_port = htons(this->udpPort);

	//Bind UDP-socket với địa chỉ Server-UDP
	if (bind(s, (sockaddr*)&serverAddrUdp, sizeof(serverAddrUdp)) == SOCKET_ERROR) {
		cerr << format("425 Can't open data connection, bind failed (WSA error: {})", WSAGetLastError()) << endl;
		closesocket(s);
		return false;
	}

	this->udpSocket.store(s); //Lưu socket vào atomic variable để các thread khác có thể truy cập và đóng an toàn
	return true;
}

bool DataChannel::rdtSend(SOCKET s, const char* data, int len, const sockaddr_in& dest) {
	uint32_t totalChunks = (len <= 0) ? 0 : (uint32_t)((len + RDT_MAX_PAYLOAD - 1) / RDT_MAX_PAYLOAD);
	vector<vector<char>> serializedPkts(totalChunks);
	for (uint32_t i = 0; i < totalChunks; i++) {
		int offset = (int)i * RDT_MAX_PAYLOAD;
		int chunkSize = min(RDT_MAX_PAYLOAD, len - offset);

		RdtPacket pkt;
		pkt.seqNum = i;
		pkt.flags = FLAG_DATA;
		pkt.checksum = 0;
		pkt.payloadLength = (uint16_t)chunkSize;
		pkt.payload.assign(data + offset, data + offset + chunkSize);
		serializedPkts[i] = serializePacket(pkt);
	}

	//Dùng timeout NGẮN (poll) cho recvfrom(): vừa cho phép vòng lặp gửi thêm gói mới trong cửa sổ, vừa tự đo thời gian thật (steady_clock) để quyết định khi nào gói "base" thực sự quá hạn.
	int pollTimeout = RDT_POLL_MS;
	setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&pollTimeout, sizeof(pollTimeout));

	uint32_t base = 0;                 //Gói CŨ NHẤT chưa được ACK (biên trái cửa sổ)
	uint32_t nextSeq = 0;              //Gói kế tiếp CHƯA từng gửi (biên phải cửa sổ)
	int window = RDT_INITIAL_WINDOW;   //Kích thước cửa sổ hiện tại (điều chỉnh theo AIMD)
	int retryRounds = 0;               //Số vòng Go-Back-N đã kích hoạt (để giới hạn RDT_MAX_RETRIES)
	bool timerRunning = false;
	chr::steady_clock::time_point timerStart;

	//PHASE 1: Gửi toàn bộ chunk DATA qua Go-Back-N
	while (base < totalChunks) {
		//Gửi thêm gói mới miễn còn nằm trong giới hạn cửa sổ
		while (nextSeq < totalChunks && nextSeq < base + (uint32_t)window) {
			if (!shouldSimulateLoss()) 
				sendto(s, serializedPkts[nextSeq].data(), (int)serializedPkts[nextSeq].size(), 0,
					(const sockaddr*)&dest, sizeof(dest));
			else cout << "[RDT-SIM] Dropped outgoing DATA packet seq=" << nextSeq << endl;
			nextSeq++;
		}

		//(Khởi động lại) timer nếu còn gói chưa được ACK và chưa có timer nào đang chạy
		//Timer LUÔN đại diện cho gói "base" hiện tại — mỗi khi base trượt, timer phải reset.
		if (!timerRunning && base < nextSeq) {
			timerStart = chr::steady_clock::now();
			timerRunning = true;
		}

		//Chờ ACK, poll ngắn mỗi vòng
		char ackBuf[RDT_HEADER_SIZE + 64];
		sockaddr_in ackFrom;
		int ackFromLen = sizeof(ackFrom);
		int ackLen = recvfrom(s, ackBuf, sizeof(ackBuf), 0, (sockaddr*)&ackFrom, &ackFromLen);

		if (ackLen == SOCKET_ERROR) {
			int err = WSAGetLastError();
			if (err != WSAETIMEDOUT) return false; //Lỗi thật (socket bị đóng bởi ABOR, v.v.)
			//Hết 1 chu kỳ poll mà chưa có ACK nào — sẽ kiểm tra timeout thật bên dưới
		}
		else if (shouldSimulateLoss()) cout << "[RDT-SIM] Dropped incoming ACK" << endl;
		
		else {
			RdtPacket ackPkt;
			if (deserializePacket(ackBuf, ackLen, ackPkt) && (ackPkt.flags & FLAG_ACK)) {
				//Cumulative ACK: ackPkt.seqNum = seq lớn nhất bên nhận đã nhận LIÊN TỤC.
				//Giá trị 0xFFFFFFFF là quy ước "receiver chưa nhận được gói nào hợp lệ".
				if (ackPkt.seqNum != 0xFFFFFFFFu && ackPkt.seqNum + 1 > base) {
					base = ackPkt.seqNum + 1;                        //Trượt cửa sổ tới
					window = min(window + 1, RDT_MAX_WINDOW);        //Additive Increase
					retryRounds = 0;                                 //Có tiến triển → reset bộ đếm lỗi
					timerRunning = false;                            //Sẽ tự khởi động lại ở vòng lặp kế nếu cần
				}
				//ACK cũ/trùng lặp (không vượt qua base hiện tại) → bỏ qua
			}
			//Gói lỗi checksum hoặc không phải ACK → bỏ qua, chờ tiếp
		}

		//Kiểm tra timeout THẬT của gói "base" (dùng đồng hồ thật, độc lập với SO_RCVTIMEO)
		if (timerRunning) {
			auto elapsedMs = chr::duration_cast<chr::milliseconds>(chr::steady_clock::now() - timerStart).count();
			if (elapsedMs >= RDT_TIMEOUT_MS) {
				retryRounds++;
				if (retryRounds > RDT_MAX_RETRIES) {
					cerr << format("[RDT] Max Go-Back-N retries reached at base={}, transfer failed", base) << endl;
					return false;
				}
				window = max(window / 2, RDT_MIN_WINDOW); //Multiplicative Decrease
				cout << "[RDT] Timeout on base seq=" << base << ", Go-Back-N retransmit ["
					<< base << ".." << (nextSeq - 1) << "], new window=" << window
					<< " (" << retryRounds << "/" << RDT_MAX_RETRIES << ")" << endl;

				//Đặc trưng Go-Back-N: gửi lại TOÀN BỘ cửa sổ hiện có, không chỉ 1 gói
				for (uint32_t i = base; i < nextSeq; i++) {
					if (!shouldSimulateLoss()) {
						sendto(s, serializedPkts[i].data(), (int)serializedPkts[i].size(), 0, (const sockaddr*)&dest, sizeof(dest));
					}
				}
				timerStart = chr::steady_clock::now();
				timerRunning = true;
			}
		}
	}

	//PHASE 2: Gửi FIN (Stop-and-Wait đơn giản, chỉ 1 gói duy nhất)
	{
		RdtPacket finPkt;
		finPkt.seqNum = totalChunks; //Báo cho receiver: "tổng cộng đã gửi totalChunks gói DATA"
		finPkt.flags = FLAG_FIN;
		finPkt.checksum = 0;
		finPkt.payloadLength = 0;
		vector<char> rawFin = serializePacket(finPkt);

		//Khôi phục timeout bình thường (không cần poll ngắn nữa vì FIN chỉ có 1 gói, không có cửa sổ)
		int timeout = RDT_TIMEOUT_MS;
		setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

		bool finAcked = false;
		for (int retry = 0; retry < RDT_MAX_RETRIES; retry++) {
			if (!shouldSimulateLoss()) {
				int sent = sendto(s, rawFin.data(), (int)rawFin.size(), 0, (const sockaddr*)&dest, sizeof(dest));
				if (sent == SOCKET_ERROR) return false;
			}
			else cout << "[RDT-SIM] Dropped outgoing FIN packet seq=" << totalChunks << endl;

			char ackBuf[RDT_HEADER_SIZE + 64];
			sockaddr_in ackFrom;
			int ackFromLen = sizeof(ackFrom);
			int ackLen = recvfrom(s, ackBuf, sizeof(ackBuf), 0, (sockaddr*)&ackFrom, &ackFromLen);

			if (ackLen == SOCKET_ERROR) {
				int err = WSAGetLastError();
				if (err == WSAETIMEDOUT) {
					cout << "[RDT] Timeout waiting for FIN-ACK, retransmit (" << (retry + 1) << "/" << RDT_MAX_RETRIES << ")" << endl;
					continue;
				}
				return false;
			}
			if (shouldSimulateLoss()) { cout << "[RDT-SIM] Dropped incoming FIN-ACK" << endl; continue; }

			RdtPacket ackPkt;
			if (!deserializePacket(ackBuf, ackLen, ackPkt)) continue;
			if ((ackPkt.flags & FLAG_ACK) && ackPkt.seqNum == totalChunks) { finAcked = true; break; }
		}

		if (!finAcked) {
			cerr << "[RDT] Max retries reached for FIN, transfer failed" << endl;
			return false;
		}
	}

	return true;
}

int DataChannel::rdtReceive(SOCKET s, std::vector<char>& outData, sockaddr_in& senderAddr) {
	uint32_t expectedSeq = 0;
	outData.clear();

	//Timeout dài cho receiver (chờ hoạt động từ sender) — sender sẽ Go-Back-N nếu ACK bị mất
	int timeout = RDT_TIMEOUT_MS * (RDT_MAX_RETRIES + 1);
	setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

	char recvBuf[RDT_HEADER_SIZE + RDT_MAX_PAYLOAD + 64];
	int senderAddrLen = sizeof(senderAddr);

	while (true) {
		int byteRecv = recvfrom(s, recvBuf, sizeof(recvBuf), 0,
			(sockaddr*)&senderAddr, &senderAddrLen);

		if (byteRecv == SOCKET_ERROR) {
			int err = WSAGetLastError();
			if (err == WSAETIMEDOUT) {
				cerr << "[RDT] Receiver timeout — sender may have disconnected" << endl;
				return -1;
			}
			return -1; //Socket bị đóng (ABOR) hoặc lỗi thật
		}
		if (byteRecv == 0) continue; //Gói rỗng bất thường → bỏ qua

		if (shouldSimulateLoss()) {
			cout << "[RDT-SIM] Dropped incoming packet (" << byteRecv << " bytes)" << endl;
			continue;
		}

		RdtPacket pkt;
		if (!deserializePacket(recvBuf, byteRecv, pkt)) {
			cout << "[RDT] Received corrupted packet, dropping (no ACK)" << endl;
			continue;
		}

		//Xử lý FIN
		if (pkt.flags & FLAG_FIN) {
			if (pkt.seqNum == expectedSeq) {
				//Đúng: đã nhận đủ toàn bộ DATA (expectedSeq == tổng số chunk sender đã gửi) → xác nhận và kết thúc
				RdtPacket ackPkt;
				ackPkt.seqNum = pkt.seqNum;
				ackPkt.flags = FLAG_ACK;
				ackPkt.checksum = 0;
				ackPkt.payloadLength = 0;
				std::vector<char> rawAck = serializePacket(ackPkt);
				sendto(s, rawAck.data(), (int)rawAck.size(), 0, (const sockaddr*)&senderAddr, sizeof(senderAddr));
				break;
			}
			else {
				//FIN đến khi còn thiếu dữ liệu ở giữa (bất thường/trùng) → bỏ qua, tiếp tục chờ DATA còn thiếu
				cout << "[RDT] Premature/duplicate FIN (seq=" << pkt.seqNum << ", expected=" << expectedSeq << "), ignoring" << endl;
				continue;
			}
		}

		//Xử lý DATA
		if (pkt.flags & FLAG_DATA) {
			if (pkt.seqNum == expectedSeq) {
				//Đúng thứ tự → deliver
				outData.insert(outData.end(), pkt.payload.begin(), pkt.payload.end());
				expectedSeq++;
			}
			else if (pkt.seqNum < expectedSeq) 
				//Gói trùng lặp (đã nhận trước đó) → không deliver lại, chỉ re-ACK
				cout << "[RDT] Duplicate DATA seq=" << pkt.seqNum
					<< " (already have up to " << (expectedSeq - 1) << "), re-ACK" << endl;
			
			else 
				//Gói đến SỚM hơn dự kiến (ngoài thứ tự) → Go-Back-N: LOẠI BỎ, không đệm lại
				cout << "[RDT] Out-of-order DATA seq=" << pkt.seqNum
					<< " (expected " << expectedSeq << "), discarding (Go-Back-N)" << endl;
			

			//Luôn gửi CUMULATIVE ACK = expectedSeq-1 (số thứ tự lớn nhất đã nhận LIÊN TỤC).
			//Nếu expectedSeq == 0 (chưa nhận đúng thứ tự gói nào) thì dùng giá trị đặc biệt
			//0xFFFFFFFF để báo "chưa có gì được nhận" (tránh underflow uint32_t).
			uint32_t cumulativeAck = (expectedSeq == 0) ? 0xFFFFFFFFu : (expectedSeq - 1);
			RdtPacket ackPkt;
			ackPkt.seqNum = cumulativeAck;
			ackPkt.flags = FLAG_ACK;
			ackPkt.checksum = 0;
			ackPkt.payloadLength = 0;
			std::vector<char> rawAck = serializePacket(ackPkt);

			if (!shouldSimulateLoss()) {
				sendto(s, rawAck.data(), (int)rawAck.size(), 0,
					(const sockaddr*)&senderAddr, sizeof(senderAddr));
			}
			else cout << "[RDT-SIM] Dropped outgoing ACK seq=" << cumulativeAck << endl;
			
		}
	}

	return (int)outData.size();
}

bool DataChannel::receiveFile(const string& filepath, bool append) {
	//Mở file để ghi dữ liệu nhận được từ Client
	ios::openmode mode = ios::binary | (append ? ios::app : ios::trunc);
	ofstream out(filepath, mode);
	if (!out.is_open()) {
		cerr << format("550 File unavailable, cannot open '{}' for writing", filepath) << endl;
		return false;
	}

	SOCKET s = udpSocket.load();
	if (s == INVALID_SOCKET) { out.close(); return false; }

	// Nhận toàn bộ dữ liệu qua RDT
	vector<char> fileData;
	sockaddr_in senderAddr;
	int totalRecv = rdtReceive(s, fileData, senderAddr);

	if (totalRecv < 0) {
		cerr << "426 Connection closed, transfer aborted" << endl;
		out.close();
		return false;
	}

	//Ghi toàn bộ dữ liệu nhận được vào file
	if (!fileData.empty()) out.write(fileData.data(), fileData.size());

	out.close();
	return true;
}

bool DataChannel::sendFile(const string& filepath, const string& destIp, unsigned short destPort) {
	//Mở file để đọc dữ liệu gửi tới Client
	ifstream in(filepath, ios::binary);
	if (!in.is_open()) {
		cerr << format("550 File unavailable, cannot open '{}' for reading", filepath) << endl;
		return false;
	}

	//Chuẩn bị địa chỉ đích (Client) để gửi dữ liệu qua UDP
	sockaddr_in destAddr;
	destAddr.sin_family = AF_INET;
	destAddr.sin_port = htons(destPort);
	inet_pton(AF_INET, destIp.c_str(), &destAddr.sin_addr);

	//Đọc toàn bộ file vào buffer
	vector<char> fileData((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
	in.close();

	SOCKET s = udpSocket.load();
	if (s == INVALID_SOCKET) return false;

	//Gửi toàn bộ file qua RDT
	return rdtSend(s, fileData.data(), (int)fileData.size(), destAddr);
}

bool DataChannel::sendFileAfterHandshake(const string& filepath) {
	SOCKET s = udpSocket.load();
	if (s == INVALID_SOCKET) return false;

	//Nhận probe qua RDT (client gửi 1 byte "R" qua rdtSend)
	vector<char> probeData;
	sockaddr_in clientAddr;
	int probeLen = rdtReceive(s, probeData, clientAddr);

	if (probeLen < 0) {
		cerr << "426 Connection closed, transfer aborted (probe failed)" << endl;
		return false;
	}

	//Chuyển đổi địa chỉ IP từ dạng nhị phân sang chuỗi
	char ipStr[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, INET_ADDRSTRLEN);
	unsigned short learnedPort = ntohs(clientAddr.sin_port);

	return sendFile(filepath, ipStr, learnedPort);
}

bool DataChannel::sendProbe(const string& destIp, unsigned short destPort) {
	SOCKET s = udpSocket.load();
	if (s == INVALID_SOCKET) return false;

	//Chuẩn bị địa chỉ đích (Server) để gửi gói tin "probe" qua UDP
	sockaddr_in destAddr;
	destAddr.sin_family = AF_INET;
	destAddr.sin_port = htons(destPort);
	inet_pton(AF_INET, destIp.c_str(), &destAddr.sin_addr);

	//Gửi probe qua RDT
	const char probe = 'R';
	return rdtSend(s, &probe, 1, destAddr);
}

void DataChannel::stop() {
	SOCKET s = udpSocket.exchange(INVALID_SOCKET); //atomic swap: chỉ 1 thread thực sự đóng
	if (s != INVALID_SOCKET) closesocket(s);
}