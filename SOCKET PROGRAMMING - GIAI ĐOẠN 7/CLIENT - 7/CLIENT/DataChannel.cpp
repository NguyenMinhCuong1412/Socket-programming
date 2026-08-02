#include "DataChannel.h"

DataChannel::DataChannel(unsigned short port) {
	this->udpPort = port;
	this->udpSocket.store(INVALID_SOCKET);
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

	//Lưu socket vào atomic variable để các thread khác có thể truy cập và đóng an toàn
	this->udpSocket.store(s);
	return true;
}

// ============================================================
// rdtSend — Gửi dữ liệu qua Stop-and-Wait ARQ
//
// Luồng:
//   1. Chia data thành các chunk ≤ RDT_MAX_PAYLOAD
//   2. Với mỗi chunk: tạo DATA packet → serialize → sendto → chờ ACK
//      - Nếu ACK đúng seq → chunk kế
//      - Nếu timeout → retransmit (tối đa RDT_MAX_RETRIES)
//      - Nếu ACK sai seq/checksum → bỏ qua, chờ tiếp
//   3. Sau khi gửi hết data: gửi FIN → chờ ACK cho FIN
// ============================================================
bool DataChannel::rdtSend(SOCKET s, const char* data, int len, const sockaddr_in& dest) {
	uint32_t seqNum = 0;
	int offset = 0;

	// ----- Thiết lập timeout cho socket (dùng cho recvfrom chờ ACK) -----
	int timeout = RDT_TIMEOUT_MS;
	setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

	// ===== PHASE 1: Gửi từng chunk DATA =====
	while (offset < len) {
		// Tính kích thước chunk hiện tại
		int chunkSize = min(RDT_MAX_PAYLOAD, len - offset);

		// Tạo DATA packet
		RdtPacket pkt;
		pkt.seqNum = seqNum;
		pkt.flags = FLAG_DATA;
		pkt.checksum = 0;
		pkt.payloadLength = (uint16_t)chunkSize;
		pkt.payload.assign(data + offset, data + offset + chunkSize);

		// Serialize packet
		std::vector<char> rawPkt = serializePacket(pkt);

		bool acked = false;
		for (int retry = 0; retry < RDT_MAX_RETRIES; retry++) {
			// Giả lập mất gói khi gửi
			if (!shouldSimulateLoss()) {
				int sent = sendto(s, rawPkt.data(), (int)rawPkt.size(), 0,
					(const sockaddr*)&dest, sizeof(dest));
				if (sent == SOCKET_ERROR) {
					cerr << "[RDT] sendto() failed (WSA error: " << WSAGetLastError() << ")" << endl;
					return false;
				}
			}
			else {
				cout << "[RDT-SIM] Dropped outgoing DATA packet seq=" << seqNum << endl;
			}

			// Chờ ACK
			char ackBuf[RDT_HEADER_SIZE + 64]; // ACK không có payload lớn
			sockaddr_in ackFrom;
			int ackFromLen = sizeof(ackFrom);

			int ackLen = recvfrom(s, ackBuf, sizeof(ackBuf), 0,
				(sockaddr*)&ackFrom, &ackFromLen);

			if (ackLen == SOCKET_ERROR) {
				int err = WSAGetLastError();
				if (err == WSAETIMEDOUT) {
					// Timeout → retransmit
					cout << "[RDT] Timeout waiting for ACK seq=" << seqNum
						<< ", retransmit (" << (retry + 1) << "/" << RDT_MAX_RETRIES << ")" << endl;
					continue;
				}
				else {
					// Lỗi thật (socket bị đóng bởi ABOR, v.v.)
					return false;
				}
			}

			// Giả lập mất gói khi nhận ACK
			if (shouldSimulateLoss()) {
				cout << "[RDT-SIM] Dropped incoming ACK" << endl;
				continue;
			}

			// Deserialize ACK
			RdtPacket ackPkt;
			if (!deserializePacket(ackBuf, ackLen, ackPkt)) {
				// Checksum lỗi hoặc packet hỏng → bỏ qua, chờ tiếp
				cout << "[RDT] Received corrupted ACK, ignoring" << endl;
				continue;
			}

			// Kiểm tra ACK đúng seq
			if ((ackPkt.flags & FLAG_ACK) && ackPkt.seqNum == seqNum) {
				acked = true;
				break; // Chuyển sang chunk kế tiếp
			}
			// ACK sai seq → bỏ qua, chờ tiếp (vẫn trong vòng retry)
		}

		if (!acked) {
			cerr << "[RDT] Max retries reached for DATA seq=" << seqNum << ", transfer failed" << endl;
			return false;
		}

		// Chuyển sang chunk tiếp theo
		offset += chunkSize;
		seqNum ^= 1; // Đổi sequence number (0 ↔ 1)
	}

	// ===== PHASE 2: Gửi FIN =====
	{
		RdtPacket finPkt;
		finPkt.seqNum = seqNum;
		finPkt.flags = FLAG_FIN;
		finPkt.checksum = 0;
		finPkt.payloadLength = 0;

		std::vector<char> rawFin = serializePacket(finPkt);

		bool finAcked = false;
		for (int retry = 0; retry < RDT_MAX_RETRIES; retry++) {
			if (!shouldSimulateLoss()) {
				int sent = sendto(s, rawFin.data(), (int)rawFin.size(), 0,
					(const sockaddr*)&dest, sizeof(dest));
				if (sent == SOCKET_ERROR) return false;
			}
			else {
				cout << "[RDT-SIM] Dropped outgoing FIN packet seq=" << seqNum << endl;
			}

			// Chờ ACK cho FIN
			char ackBuf[RDT_HEADER_SIZE + 64];
			sockaddr_in ackFrom;
			int ackFromLen = sizeof(ackFrom);

			int ackLen = recvfrom(s, ackBuf, sizeof(ackBuf), 0,
				(sockaddr*)&ackFrom, &ackFromLen);

			if (ackLen == SOCKET_ERROR) {
				int err = WSAGetLastError();
				if (err == WSAETIMEDOUT) {
					cout << "[RDT] Timeout waiting for FIN-ACK seq=" << seqNum
						<< ", retransmit (" << (retry + 1) << "/" << RDT_MAX_RETRIES << ")" << endl;
					continue;
				}
				else {
					return false;
				}
			}

			if (shouldSimulateLoss()) {
				cout << "[RDT-SIM] Dropped incoming FIN-ACK" << endl;
				continue;
			}

			RdtPacket ackPkt;
			if (!deserializePacket(ackBuf, ackLen, ackPkt)) continue;

			if ((ackPkt.flags & FLAG_ACK) && ackPkt.seqNum == seqNum) {
				finAcked = true;
				break;
			}
		}

		if (!finAcked) {
			cerr << "[RDT] Max retries reached for FIN, transfer failed" << endl;
			return false;
		}
	}

	return true;
}

// ============================================================
// rdtReceive — Nhận dữ liệu qua Stop-and-Wait ARQ
//
// Luồng:
//   1. Vòng lặp recvfrom()
//   2. Deserialize + kiểm tra checksum
//      - Checksum sai → DROP, không ACK
//   3. Nếu FIN → gửi ACK → break
//   4. Nếu DATA:
//      - seq == expectedSeq → deliver (append vào outData) → đổi expectedSeq
//      - seq != expectedSeq → duplicate, không deliver
//      - Gửi ACK(seq nhận được) trong cả 2 trường hợp
//
// Return: tổng byte nhận được, -1 nếu lỗi
// ============================================================
int DataChannel::rdtReceive(SOCKET s, std::vector<char>& outData, sockaddr_in& senderAddr) {
	uint32_t expectedSeq = 0;
	outData.clear();

	// Thiết lập timeout dài hơn cho receiver (chờ data từ sender)
	// Dùng timeout lớn vì sender sẽ retransmit nếu ACK mất
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
			// Socket bị đóng (ABOR) hoặc lỗi thật
			return -1;
		}
		if (byteRecv == 0) continue; // Gói rỗng bất thường → bỏ qua

		// Giả lập mất gói khi nhận
		if (shouldSimulateLoss()) {
			cout << "[RDT-SIM] Dropped incoming packet (" << byteRecv << " bytes)" << endl;
			continue;
		}

		// Deserialize packet
		RdtPacket pkt;
		if (!deserializePacket(recvBuf, byteRecv, pkt)) {
			// Checksum sai hoặc packet hỏng → DROP, KHÔNG gửi ACK
			cout << "[RDT] Received corrupted packet, dropping (no ACK)" << endl;
			continue;
		}

		// ----- Xử lý FIN -----
		if (pkt.flags & FLAG_FIN) {
			// Gửi ACK cho FIN
			RdtPacket ackPkt;
			ackPkt.seqNum = pkt.seqNum;
			ackPkt.flags = FLAG_ACK;
			ackPkt.checksum = 0;
			ackPkt.payloadLength = 0;
			std::vector<char> rawAck = serializePacket(ackPkt);

			sendto(s, rawAck.data(), (int)rawAck.size(), 0,
				(const sockaddr*)&senderAddr, sizeof(senderAddr));

			break; // Kết thúc nhận
		}

		// ----- Xử lý DATA -----
		if (pkt.flags & FLAG_DATA) {
			if (pkt.seqNum == expectedSeq) {
				// Gói mới → deliver (append vào buffer)
				outData.insert(outData.end(), pkt.payload.begin(), pkt.payload.end());
				expectedSeq ^= 1; // Đổi expected sequence (0 ↔ 1)
			}
			else {
				// Gói duplicate → không deliver, nhưng vẫn ACK
				cout << "[RDT] Duplicate DATA seq=" << pkt.seqNum
					<< " (expected " << expectedSeq << "), ACK but no deliver" << endl;
			}

			// Gửi ACK (cả trường hợp đúng seq lẫn duplicate)
			RdtPacket ackPkt;
			ackPkt.seqNum = pkt.seqNum; // ACK theo seq nhận được
			ackPkt.flags = FLAG_ACK;
			ackPkt.checksum = 0;
			ackPkt.payloadLength = 0;
			std::vector<char> rawAck = serializePacket(ackPkt);

			// Giả lập mất gói khi gửi ACK
			if (!shouldSimulateLoss()) {
				sendto(s, rawAck.data(), (int)rawAck.size(), 0,
					(const sockaddr*)&senderAddr, sizeof(senderAddr));
			}
			else {
				cout << "[RDT-SIM] Dropped outgoing ACK seq=" << pkt.seqNum << endl;
			}
		}
	}

	return (int)outData.size();
}

// ============================================================
// receiveFile — Nhận file qua RDT
//
// THAY ĐỔI SO VỚI GIAI ĐOẠN 5:
//   - Trước: vòng lặp recvfrom() trực tiếp, gói 0-byte = EOF
//   - Sau: gọi rdtReceive() → nhận toàn bộ data → ghi file
// ============================================================
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
	std::vector<char> fileData;
	sockaddr_in senderAddr;
	int totalRecv = rdtReceive(s, fileData, senderAddr);

	if (totalRecv < 0) {
		cerr << "426 Connection closed, transfer aborted" << endl;
		out.close();
		return false;
	}

	// Ghi toàn bộ dữ liệu nhận được vào file
	if (!fileData.empty()) {
		out.write(fileData.data(), fileData.size());
	}

	out.close();
	return true;
}

// ============================================================
// sendFile — Gửi file qua RDT
//
// THAY ĐỔI SO VỚI GIAI ĐOẠN 5:
//   - Trước: sendto() từng chunk + gói 0-byte EOF
//   - Sau: đọc toàn bộ file vào buffer → gọi rdtSend()
//          (rdtSend tự chia chunk, gửi FIN thay gói 0-byte)
// ============================================================
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

	// Đọc toàn bộ file vào buffer
	std::vector<char> fileData((std::istreambuf_iterator<char>(in)),
		std::istreambuf_iterator<char>());
	in.close();

	SOCKET s = udpSocket.load();
	if (s == INVALID_SOCKET) return false;

	// Gửi toàn bộ file qua RDT
	return rdtSend(s, fileData.data(), (int)fileData.size(), destAddr);
}

// ============================================================
// sendFileAfterHandshake — PASSIVE + RETR
//
// THAY ĐỔI SO VỚI GIAI ĐOẠN 5:
//   - Trước: recvfrom() raw probe → sendFile() raw
//   - Sau: rdtReceive() probe → lấy địa chỉ client → sendFile() qua RDT
// ============================================================
bool DataChannel::sendFileAfterHandshake(const string& filepath) {
	SOCKET s = udpSocket.load();
	if (s == INVALID_SOCKET) return false;

	// Nhận probe qua RDT (client gửi 1 byte "R" qua rdtSend)
	std::vector<char> probeData;
	sockaddr_in clientAddr;
	int probeLen = rdtReceive(s, probeData, clientAddr);

	if (probeLen < 0) {
		cerr << "426 Connection closed, transfer aborted (probe failed)" << endl;
		return false;
	}

	// Chuyển đổi địa chỉ IP từ dạng nhị phân sang chuỗi
	char ipStr[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, INET_ADDRSTRLEN);
	unsigned short learnedPort = ntohs(clientAddr.sin_port);

	return sendFile(filepath, ipStr, learnedPort);
}

// ============================================================
// sendProbe — PASSIVE + RETR (phía Client)
//
// THAY ĐỔI SO VỚI GIAI ĐOẠN 5:
//   - Trước: sendto() 1 byte "R" raw
//   - Sau: rdtSend() 1 byte "R" qua RDT (có ACK + checksum)
// ============================================================
bool DataChannel::sendProbe(const string& destIp, unsigned short destPort) {
	SOCKET s = udpSocket.load();
	if (s == INVALID_SOCKET) return false;

	//Chuẩn bị địa chỉ đích (Server) để gửi gói tin "probe" qua UDP
	sockaddr_in destAddr;
	destAddr.sin_family = AF_INET;
	destAddr.sin_port = htons(destPort);
	inet_pton(AF_INET, destIp.c_str(), &destAddr.sin_addr);

	// Gửi probe qua RDT
	const char probe = 'R';
	return rdtSend(s, &probe, 1, destAddr);
}

void DataChannel::stop() {
	SOCKET s = udpSocket.exchange(INVALID_SOCKET); // atomic swap: chỉ 1 thread thực sự đóng
	if (s != INVALID_SOCKET) closesocket(s);
}