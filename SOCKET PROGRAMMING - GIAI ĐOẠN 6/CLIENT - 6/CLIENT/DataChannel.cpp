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
// rdtSend — Gửi dữ liệu qua Go-Back-N Sliding Window + Congestion Control
//
// Giai đoạn 7:
//   1. Chia data thành N chunk, tạo N gói DATA (seq=0..N-1) + 1 gói FIN (seq=N)
//   2. Cửa sổ trượt dựa vào base và nextSeqNum, giới hạn bởi cwnd
//   3. Congestion Control:
//      - Slow Start (cwnd < ssthresh): cwnd tăng theo số gói được ACK
//      - Congestion Avoidance (cwnd >= ssthresh): cwnd tăng 1 per RTT
//      - Multiplicative Decrease (Timeout): ssthresh = max(cwnd / 2, 1), cwnd = 1
//   4. Go-Back-N Retransmission: Khi timeout, nextSeqNum = base (gửi lại toàn bộ cửa sổ)
// ============================================================
bool DataChannel::rdtSend(SOCKET s, const char* data, int len, const sockaddr_in& dest) {
	// ----- BƯỚC 1: Chuẩn bị tất cả các packet (DATA + FIN) -----
	vector<vector<char>> packets;
	int offset = 0;
	uint32_t seq = 0;

	while (offset < len) {
		int chunkSize = min(RDT_MAX_PAYLOAD, len - offset);
		RdtPacket pkt;
		pkt.seqNum = seq++;
		pkt.flags = FLAG_DATA;
		pkt.checksum = 0;
		pkt.payloadLength = (uint16_t)chunkSize;
		pkt.payload.assign(data + offset, data + offset + chunkSize);
		packets.push_back(serializePacket(pkt));
		offset += chunkSize;
	}

	// Gói FIN ở cuối chuỗi truyền
	RdtPacket finPkt;
	finPkt.seqNum = seq++;
	finPkt.flags = FLAG_FIN;
	finPkt.checksum = 0;
	finPkt.payloadLength = 0;
	packets.push_back(serializePacket(finPkt));

	uint32_t totalPackets = (uint32_t)packets.size();
	uint32_t base = 0;
	uint32_t nextSeqNum = 0;

	// Congestion Control State
	int cwnd = 1;
	int ssthresh = GBN_INITIAL_SSTHRESH;
	int consecutiveTimeouts = 0;

	// Thiết lập timeout ngắn cho socket khi nhận ACK
	int timeout = RDT_TIMEOUT_MS;
	setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

	cout << format("[GBN-SEND] Start transfer: total {} packets, initial cwnd={}, ssthresh={}\n",
		totalPackets, cwnd, ssthresh);

	// ----- BƯỚC 2: Vòng lặp truyền Go-Back-N -----
	while (base < totalPackets) {
		// PHASE 1: Gửi tất cả các gói trong cửa sổ [nextSeqNum, base + cwnd)
		while (nextSeqNum < base + (uint32_t)cwnd && nextSeqNum < totalPackets) {
			if (!shouldSimulateLoss()) {
				int sent = sendto(s, packets[nextSeqNum].data(), (int)packets[nextSeqNum].size(), 0,
					(const sockaddr*)&dest, sizeof(dest));
				if (sent == SOCKET_ERROR) {
					cerr << "[GBN] sendto() failed (WSA error: " << WSAGetLastError() << ")" << endl;
					return false;
				}
			}
			else {
				cout << format("[GBN-SIM] Dropped outgoing packet seq={}\n", nextSeqNum);
			}
			nextSeqNum++;
		}

		// PHASE 2: Nhận ACK
		char ackBuf[RDT_HEADER_SIZE + 64];
		sockaddr_in ackFrom;
		int ackFromLen = sizeof(ackFrom);

		int ackLen = recvfrom(s, ackBuf, sizeof(ackBuf), 0, (sockaddr*)&ackFrom, &ackFromLen);

		if (ackLen == SOCKET_ERROR) {
			int err = WSAGetLastError();
			if (err == WSAETIMEDOUT) {
				// TIMEOUT: Giảm cửa sổ nghẽn (Multiplicative Decrease) & Reset về 1 (Slow Start)
				ssthresh = max(cwnd / 2, 1);
				cwnd = 1;
				consecutiveTimeouts++;

				cout << format("[GBN-TIMEOUT] Timeout at base={}! Retransmit from base. New ssthresh={}, cwnd=1 (retry {}/{})\n",
					base, ssthresh, consecutiveTimeouts, RDT_MAX_RETRIES);

				if (consecutiveTimeouts > RDT_MAX_RETRIES) {
					cerr << "[GBN] Max retries reached, transfer failed!" << endl;
					return false;
				}

				// Go-Back-N: quay lại gửi lại từ base
				nextSeqNum = base;
				continue;
			}
			else {
				// Socket bị đóng (ví dụ ABOR) hoặc lỗi hệ thống khác
				return false;
			}
		}

		// Giả lập mất gói khi nhận ACK
		if (shouldSimulateLoss()) {
			cout << "[GBN-SIM] Dropped incoming ACK\n";
			continue;
		}

		// Deserialize ACK packet
		RdtPacket ackPkt;
		if (!deserializePacket(ackBuf, ackLen, ackPkt)) {
			cout << "[GBN] Corrupted ACK received, ignoring\n";
			continue;
		}

		// Kiểm tra ACK hợp lệ
		if ((ackPkt.flags & FLAG_ACK) && ackPkt.seqNum >= base && ackPkt.seqNum < totalPackets) {
			uint32_t ackedCount = ackPkt.seqNum - base + 1;
			base = ackPkt.seqNum + 1; // Trượt cửa sổ (Cumulative ACK)
			consecutiveTimeouts = 0;  // Reset đếm timeout

			// Cập nhật Congestion Window (cwnd)
			if (cwnd < ssthresh) {
				// Slow Start: Tăng cwnd theo số gói được ACK
				cwnd += (int)ackedCount;
			}
			else {
				// Congestion Avoidance: Tăng cwnd tuyến tính (1 gói per RTT batch)
				cwnd += 1;
			}
			cwnd = min(cwnd, GBN_MAX_WINDOW);
		}
	}

	cout << "[GBN-SEND] Transfer completed successfully!\n";
	return true;
}

// ============================================================
// rdtReceive — Nhận dữ liệu qua Go-Back-N Sliding Window
//
// Giai đoạn 7:
//   1. Giữ expectedSeq (bắt đầu từ 0)
//   2. Nhận packet:
//      - Nếu checksum sai -> Drop im lặng (không ACK)
//      - Nếu seqNum == expectedSeq -> Accept payload, expectedSeq++, gửi ACK(seqNum)
//      - Nếu seqNum != expectedSeq (Out of order hoặc Duplicate) -> Drop payload,
//        gửi Cumulative ACK(expectedSeq - 1) để thông báo cho sender gói đúng gần nhất
//   3. Khi nhận gói FIN với seqNum == expectedSeq -> Gửi ACK(FIN) -> Hoàn tất.
// ============================================================
int DataChannel::rdtReceive(SOCKET s, std::vector<char>& outData, sockaddr_in& senderAddr) {
	uint32_t expectedSeq = 0;
	outData.clear();

	// Thiết lập timeout dài cho receiver
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
				cerr << "[GBN-RECV] Receiver timeout — sender stopped transmitting\n";
				return -1;
			}
			return -1;
		}
		if (byteRecv == 0) continue;

		// Giả lập mất gói khi nhận
		if (shouldSimulateLoss()) {
			cout << format("[GBN-SIM] Dropped incoming packet (len={})\n", byteRecv);
			continue;
		}

		// Deserialize packet
		RdtPacket pkt;
		if (!deserializePacket(recvBuf, byteRecv, pkt)) {
			cout << "[GBN-RECV] Corrupted packet received, dropping without ACK\n";
			continue;
		}

		// ----- Xử lý gói FIN -----
		if (pkt.flags & FLAG_FIN) {
			if (pkt.seqNum == expectedSeq || pkt.seqNum + 1 == expectedSeq) {
				// Gửi ACK cho gói FIN
				RdtPacket ackPkt;
				ackPkt.seqNum = pkt.seqNum;
				ackPkt.flags = FLAG_ACK;
				ackPkt.checksum = 0;
				ackPkt.payloadLength = 0;
				vector<char> rawAck = serializePacket(ackPkt);

				sendto(s, rawAck.data(), (int)rawAck.size(), 0, (const sockaddr*)&senderAddr, sizeof(senderAddr));

				if (pkt.seqNum == expectedSeq) {
					break; // Nhận FIN hợp lệ đúng thứ tự -> Thoát vòng lặp
				}
			}
			continue;
		}

		// ----- Xử lý gói DATA -----
		if (pkt.flags & FLAG_DATA) {
			if (pkt.seqNum == expectedSeq) {
				// Đúng gói mong đợi -> nhận dữ liệu
				outData.insert(outData.end(), pkt.payload.begin(), pkt.payload.end());
				expectedSeq++;

				// Gửi ACK cho gói vừa nhận
				RdtPacket ackPkt;
				ackPkt.seqNum = pkt.seqNum;
				ackPkt.flags = FLAG_ACK;
				ackPkt.checksum = 0;
				ackPkt.payloadLength = 0;
				vector<char> rawAck = serializePacket(ackPkt);

				if (!shouldSimulateLoss()) {
					sendto(s, rawAck.data(), (int)rawAck.size(), 0, (const sockaddr*)&senderAddr, sizeof(senderAddr));
				}
				else {
					cout << format("[GBN-SIM] Dropped outgoing ACK seq={}\n", pkt.seqNum);
				}
			}
			else {
				// Sai thứ tự hoặc trùng lặp -> Gửi lại Cumulative ACK của gói đúng gần nhất
				cout << format("[GBN-RECV] Out of order packet seq={} (expected {}), re-sending ACK for {}\n",
					pkt.seqNum, expectedSeq, expectedSeq > 0 ? expectedSeq - 1 : 0);

				if (expectedSeq > 0) {
					RdtPacket ackPkt;
					ackPkt.seqNum = expectedSeq - 1;
					ackPkt.flags = FLAG_ACK;
					ackPkt.checksum = 0;
					ackPkt.payloadLength = 0;
					vector<char> rawAck = serializePacket(ackPkt);

					if (!shouldSimulateLoss()) {
						sendto(s, rawAck.data(), (int)rawAck.size(), 0, (const sockaddr*)&senderAddr, sizeof(senderAddr));
					}
				}
			}
		}
	}

	return (int)outData.size();
}

// ============================================================
// receiveFile — Nhận file qua RDT (Go-Back-N)
// ============================================================
bool DataChannel::receiveFile(const string& filepath, bool append) {
	ios::openmode mode = ios::binary | (append ? ios::app : ios::trunc);
	ofstream out(filepath, mode);
	if (!out.is_open()) {
		cerr << format("550 File unavailable, cannot open '{}' for writing", filepath) << endl;
		return false;
	}

	SOCKET s = udpSocket.load();
	if (s == INVALID_SOCKET) { out.close(); return false; }

	// Nhận toàn bộ dữ liệu qua Go-Back-N RDT
	std::vector<char> fileData;
	sockaddr_in senderAddr;
	int totalRecv = rdtReceive(s, fileData, senderAddr);

	if (totalRecv < 0) {
		cerr << "426 Connection closed, transfer aborted" << endl;
		out.close();
		return false;
	}

	if (!fileData.empty()) {
		out.write(fileData.data(), fileData.size());
	}

	out.close();
	return true;
}

// ============================================================
// sendFile — Gửi file qua RDT (Go-Back-N)
// ============================================================
bool DataChannel::sendFile(const string& filepath, const string& destIp, unsigned short destPort) {
	ifstream in(filepath, ios::binary);
	if (!in.is_open()) {
		cerr << format("550 File unavailable, cannot open '{}' for reading", filepath) << endl;
		return false;
	}

	sockaddr_in destAddr;
	destAddr.sin_family = AF_INET;
	destAddr.sin_port = htons(destPort);
	inet_pton(AF_INET, destIp.c_str(), &destAddr.sin_addr);

	std::vector<char> fileData((std::istreambuf_iterator<char>(in)),
		std::istreambuf_iterator<char>());
	in.close();

	SOCKET s = udpSocket.load();
	if (s == INVALID_SOCKET) return false;

	return rdtSend(s, fileData.data(), (int)fileData.size(), destAddr);
}

// ============================================================
// sendFileAfterHandshake — PASSIVE + RETR
// ============================================================
bool DataChannel::sendFileAfterHandshake(const string& filepath) {
	SOCKET s = udpSocket.load();
	if (s == INVALID_SOCKET) return false;

	std::vector<char> probeData;
	sockaddr_in clientAddr;
	int probeLen = rdtReceive(s, probeData, clientAddr);

	if (probeLen < 0) {
		cerr << "426 Connection closed, transfer aborted (probe failed)" << endl;
		return false;
	}

	char ipStr[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, INET_ADDRSTRLEN);
	unsigned short learnedPort = ntohs(clientAddr.sin_port);

	return sendFile(filepath, ipStr, learnedPort);
}

// ============================================================
// sendProbe — PASSIVE + RETR (phía Client)
// ============================================================
bool DataChannel::sendProbe(const string& destIp, unsigned short destPort) {
	SOCKET s = udpSocket.load();
	if (s == INVALID_SOCKET) return false;

	sockaddr_in destAddr;
	destAddr.sin_family = AF_INET;
	destAddr.sin_port = htons(destPort);
	inet_pton(AF_INET, destIp.c_str(), &destAddr.sin_addr);

	const char probe = 'R';
	return rdtSend(s, &probe, 1, destAddr);
}

void DataChannel::stop() {
	SOCKET s = udpSocket.exchange(INVALID_SOCKET);
	if (s != INVALID_SOCKET) closesocket(s);
}