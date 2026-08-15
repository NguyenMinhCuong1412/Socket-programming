// ======================================================================
// DataChannel.cpp — CÀI ĐẶT KÊNH DỮ LIỆU (UDP) CỦA SERVER
//    Triển khai truyền file tin cậy qua UDP bằng giao thức RDT:
//    - rdtSend: gửi dữ liệu bằng Go-Back-N Sliding Window + AIMD
//    - rdtReceive: nhận dữ liệu với cumulative ACK
//    - sendFile/receiveFile: giao diện cấp cao đọc/ghi file
//    - sendProbe/sendFileAfterHandshake: cơ chế bắt tay Passive mode
//    Khác với Client: không có thanh tiến trình (Server chạy headless),
//    và receiveFile hỗ trợ append + kiểm tra newline cuối file (APPE).
// ======================================================================
#include "DataChannel.h"

// Constructor: khởi tạo DataChannel với port dự kiến bind
DataChannel::DataChannel(unsigned short port) {
	this->udpPort = port;
	this->udpSocket.store(INVALID_SOCKET);
}

// Lấy port thực tế đã bind — dùng getsockname() khi port=0 (OS tự chọn)
unsigned short DataChannel::getBoundPort() const {
	SOCKET s = udpSocket.load();
	if (s == INVALID_SOCKET) return 0;
	sockaddr_in local = {};
	int len = sizeof(local);
	if (getsockname(s, (sockaddr*)&local, &len) == SOCKET_ERROR) return 0;
	return ntohs(local.sin_port);  // ntohs: network → host byte order cho port
}

// Tạo socket UDP và bind
bool DataChannel::start() {
	SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (s == INVALID_SOCKET) {
		cerr << format("425 Can't open data connection, cannot create socket (WSA error: {})", WSAGetLastError()) << endl;
		return false;
	}

	// Cho phép tái sử dụng port — tránh lỗi khi bind lại nhanh
	BOOL reuse = TRUE;
	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

	sockaddr_in serverAddrUdp = {};
	serverAddrUdp.sin_family = AF_INET;
	serverAddrUdp.sin_addr.s_addr = INADDR_ANY;       // Lắng nghe trên mọi interface
	serverAddrUdp.sin_port = htons(this->udpPort);     // htons: host → network byte order

	if (bind(s, (sockaddr*)&serverAddrUdp, sizeof(serverAddrUdp)) == SOCKET_ERROR) {
		cerr << format("425 Can't open data connection, bind failed (WSA error: {})", WSAGetLastError()) << endl;
		closesocket(s);
		return false;
	}

	this->udpSocket.store(s);
	return true;
}

// =====================================================================
// rdtSend (phiên bản buffer) — GỬI DỮ LIỆU TIN CẬY QUA GO-BACK-N
//   Logic giống Client nhưng không có thanh tiến trình.
//   Chia dữ liệu → serialize → gửi theo cửa sổ trượt → chờ ACK tích lũy
//   → timeout → retransmit toàn bộ cửa sổ → AIMD điều chỉnh window
//   → kết thúc bằng FIN/FIN-ACK
// =====================================================================
bool DataChannel::rdtSend(SOCKET s, const char* data, size_t len, const sockaddr_in& dest) {
	uint32_t totalChunks = (len == 0) ? 0 : (uint32_t)((len + RDT_MAX_PAYLOAD - 1) / RDT_MAX_PAYLOAD);
	// Pre-serialize tất cả gói DATA
	vector<vector<char>> serializedPkts(totalChunks);
	for (uint32_t i = 0; i < totalChunks; i++) {
		size_t offset = (size_t)i * RDT_MAX_PAYLOAD;
		size_t chunkSize = min((size_t)RDT_MAX_PAYLOAD, len - offset);

		RdtPacket pkt;
		pkt.seqNum = i;
		pkt.flags = FLAG_DATA;
		pkt.checksum = 0;
		pkt.payloadLength = (uint16_t)chunkSize;
		pkt.payload.assign(data + offset, data + offset + chunkSize);
		serializedPkts[i] = serializePacket(pkt);
	}

	// Đặt timeout nhận ACK = RDT_POLL_MS
	int pollTimeout = RDT_POLL_MS;
	setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&pollTimeout, sizeof(pollTimeout));

	uint32_t base = 0;        // Gói đầu cửa sổ chưa được ACK
	uint32_t nextSeq = 0;     // Gói tiếp theo cần gửi
	int window = RDT_INITIAL_WINDOW;
	int retryRounds = 0;
	bool timerRunning = false;
	chr::steady_clock::time_point timerStart;

	// Vòng lặp Go-Back-N chính
	while (base < totalChunks) {
		// Gửi các gói trong phạm vi cửa sổ
		while (nextSeq < totalChunks && nextSeq < base + (uint32_t)window) {
			if (!shouldSimulateLoss()) {
				int sent = sendto(s, serializedPkts[nextSeq].data(), (int)serializedPkts[nextSeq].size(), 0,
					(const sockaddr*)&dest, sizeof(dest));
				if (sent == SOCKET_ERROR) {
					int err = WSAGetLastError();
					if (err != WSAECONNRESET) {
						cerr << "[RDT] sendto failed with error: " << err << endl;
						return false;
					}
				}
			}
			else cout << "[RDT-SIM] Dropped outgoing DATA packet seq=" << nextSeq << endl;
			nextSeq++;
		}

		// Bắt đầu timer
		if (!timerRunning && base < nextSeq) {
			timerStart = chr::steady_clock::now();
			timerRunning = true;
		}

		// Chờ nhận ACK
		char ackBuf[RDT_HEADER_SIZE + 64];
		sockaddr_in ackFrom = {};
		int ackFromLen = sizeof(ackFrom);
		int ackLen = recvfrom(s, ackBuf, sizeof(ackBuf), 0, (sockaddr*)&ackFrom, &ackFromLen);

		if (ackLen == SOCKET_ERROR) {
			int err = WSAGetLastError();
			if (err == WSAECONNRESET) {
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
				continue;
			}
			if (err != WSAETIMEDOUT) return false;
		}
		else if (shouldSimulateLoss()) cout << "[RDT-SIM] Dropped incoming ACK" << endl;

		else {
			// Kiểm tra ACK đến từ đúng địa chỉ đích
			if (ackFrom.sin_addr.s_addr != dest.sin_addr.s_addr ||
				ackFrom.sin_port != dest.sin_port) {
				continue;
			}

			RdtPacket ackPkt;
			if (deserializePacket(ackBuf, ackLen, ackPkt) && (ackPkt.flags & FLAG_ACK)) {
				// Cumulative ACK: ACK(n) → tất cả gói ≤ n đã được nhận
				// 0xFFFFFFFF: chưa nhận gói nào
				if (ackPkt.seqNum != 0xFFFFFFFFu && ackPkt.seqNum + 1 > base) {
					base = ackPkt.seqNum + 1;
					window = min(window + 1, RDT_MAX_WINDOW);  // Additive Increase
					retryRounds = 0;
					timerRunning = false;
				}
			}
		}

		// Kiểm tra timeout → Go-Back-N retransmit
		if (timerRunning) {
			auto elapsedMs = chr::duration_cast<chr::milliseconds>(chr::steady_clock::now() - timerStart).count();
			if (elapsedMs >= RDT_TIMEOUT_MS) {
				retryRounds++;
				if (retryRounds > RDT_MAX_RETRIES) {
					cerr << format("[RDT] Max Go-Back-N retries reached at base={}, transfer failed", base) << endl;
					return false;
				}
				window = max(window / 2, RDT_MIN_WINDOW);  // Multiplicative Decrease
				cout << "[RDT] Timeout on base seq=" << base << ", Go-Back-N retransmit ["
					<< base << ".." << (nextSeq - 1) << "], new window=" << window
					<< " (" << retryRounds << "/" << RDT_MAX_RETRIES << ")" << endl;

				// Retransmit toàn bộ cửa sổ
				for (uint32_t i = base; i < nextSeq; i++) {
					if (!shouldSimulateLoss()) {
						int sent = sendto(s, serializedPkts[i].data(), (int)serializedPkts[i].size(), 0, (const sockaddr*)&dest, sizeof(dest));
						if (sent == SOCKET_ERROR) {
							int err = WSAGetLastError();
							if (err != WSAECONNRESET) {
								cerr << "[RDT] GBN sendto failed: " << err << endl;
								return false;
							}
						}
					}
				}
				timerStart = chr::steady_clock::now();
				timerRunning = true;
			}
		}
	}

	// === Gửi FIN → chờ FIN-ACK ===
	{
		RdtPacket finPkt;
		finPkt.seqNum = totalChunks;
		finPkt.flags = FLAG_FIN;
		finPkt.checksum = 0;
		finPkt.payloadLength = 0;
		vector<char> rawFin = serializePacket(finPkt);

		int timeout = RDT_TIMEOUT_MS;
		setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

		bool finAcked = false;
		for (int retry = 0; retry < RDT_MAX_RETRIES; retry++) {
			if (!shouldSimulateLoss()) {
				int sent = sendto(s, rawFin.data(), (int)rawFin.size(), 0, (const sockaddr*)&dest, sizeof(dest));
				if (sent == SOCKET_ERROR) {
					int err = WSAGetLastError();
					if (err != WSAECONNRESET) return false;
				}
			}
			else cout << "[RDT-SIM] Dropped outgoing FIN packet seq=" << totalChunks << endl;

			char ackBuf[RDT_HEADER_SIZE + 64];
			sockaddr_in ackFrom = {};
			int ackFromLen = sizeof(ackFrom);
			int ackLen = recvfrom(s, ackBuf, sizeof(ackBuf), 0, (sockaddr*)&ackFrom, &ackFromLen);

			if (ackLen == SOCKET_ERROR) {
				int err = WSAGetLastError();
				if (err == WSAECONNRESET) {
					std::this_thread::sleep_for(std::chrono::milliseconds(50));
					continue;
				}
				if (err == WSAETIMEDOUT) {
					cout << "[RDT] Timeout waiting for FIN-ACK, retransmit (" << (retry + 1) << "/" << RDT_MAX_RETRIES << ")" << endl;
					continue;
				}
				return false;
			}
			if (shouldSimulateLoss()) { cout << "[RDT-SIM] Dropped incoming FIN-ACK" << endl; continue; }

			if (ackFrom.sin_addr.s_addr != dest.sin_addr.s_addr ||
				ackFrom.sin_port != dest.sin_port) {
				continue;
			}

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

// =====================================================================
// rdtSend (phiên bản ifstream) — GỬI FILE LỚN QUA RDT
//   Đọc dữ liệu từ file stream theo chunk khi cần gửi/retransmit
//   (không nạp toàn bộ file vào RAM). Logic Go-Back-N giống phiên bản buffer.
// =====================================================================
bool DataChannel::rdtSend(SOCKET s, std::ifstream& in, uintmax_t len, const sockaddr_in& dest) {
	uint32_t totalChunks = (len == 0) ? 0 : (uint32_t)((len + RDT_MAX_PAYLOAD - 1) / RDT_MAX_PAYLOAD);

	// Lambda đọc chunk từ file theo seq → serialize thành gói RDT
	auto getSerializedPacket = [&](uint32_t seq) -> vector<char> {
		size_t offset = (size_t)seq * RDT_MAX_PAYLOAD;
		size_t chunkSize = min((size_t)RDT_MAX_PAYLOAD, (size_t)(len - offset));

		RdtPacket pkt;
		pkt.seqNum = seq;
		pkt.flags = FLAG_DATA;
		pkt.checksum = 0;
		pkt.payloadLength = (uint16_t)chunkSize;
		pkt.payload.resize(chunkSize);

		in.clear();
		in.seekg(offset, ios::beg);
		in.read(pkt.payload.data(), chunkSize);

		return serializePacket(pkt);
	};

	int pollTimeout = RDT_POLL_MS;
	setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&pollTimeout, sizeof(pollTimeout));

	uint32_t base = 0;
	uint32_t nextSeq = 0;
	int window = RDT_INITIAL_WINDOW;
	int retryRounds = 0;
	bool timerRunning = false;
	chr::steady_clock::time_point timerStart;

	// Vòng lặp Go-Back-N
	while (base < totalChunks) {
		while (nextSeq < totalChunks && nextSeq < base + (uint32_t)window) {
			if (!shouldSimulateLoss()) {
				vector<char> serializedPkt = getSerializedPacket(nextSeq);
				int sent = sendto(s, serializedPkt.data(), (int)serializedPkt.size(), 0,
					(const sockaddr*)&dest, sizeof(dest));
				if (sent == SOCKET_ERROR) {
					int err = WSAGetLastError();
					if (err != WSAECONNRESET) {
						cerr << "[RDT] sendto failed with error: " << err << endl;
						return false;
					}
				}
			}
			else cout << "[RDT-SIM] Dropped outgoing DATA packet seq=" << nextSeq << endl;
			nextSeq++;
		}

		if (!timerRunning && base < nextSeq) {
			timerStart = chr::steady_clock::now();
			timerRunning = true;
		}

		char ackBuf[RDT_HEADER_SIZE + 64];
		sockaddr_in ackFrom = {};
		int ackFromLen = sizeof(ackFrom);
		int ackLen = recvfrom(s, ackBuf, sizeof(ackBuf), 0, (sockaddr*)&ackFrom, &ackFromLen);

		if (ackLen == SOCKET_ERROR) {
			int err = WSAGetLastError();
			if (err == WSAECONNRESET) {
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
				continue;
			}
			if (err != WSAETIMEDOUT) return false;
		}
		else if (shouldSimulateLoss()) cout << "[RDT-SIM] Dropped incoming ACK" << endl;

		else {
			if (ackFrom.sin_addr.s_addr != dest.sin_addr.s_addr ||
				ackFrom.sin_port != dest.sin_port) {
				continue;
			}

			RdtPacket ackPkt;
			if (deserializePacket(ackBuf, ackLen, ackPkt) && (ackPkt.flags & FLAG_ACK)) {
				if (ackPkt.seqNum != 0xFFFFFFFFu && ackPkt.seqNum + 1 > base) {
					base = ackPkt.seqNum + 1;
					window = min(window + 1, RDT_MAX_WINDOW);
					retryRounds = 0;
					timerRunning = false;
				}
			}
		}

		if (timerRunning) {
			auto elapsedMs = chr::duration_cast<chr::milliseconds>(chr::steady_clock::now() - timerStart).count();
			if (elapsedMs >= RDT_TIMEOUT_MS) {
				retryRounds++;
				if (retryRounds > RDT_MAX_RETRIES) {
					cerr << format("[RDT] Max Go-Back-N retries reached at base={}, transfer failed", base) << endl;
					return false;
				}
				window = max(window / 2, RDT_MIN_WINDOW);
				cout << "[RDT] Timeout on base seq=" << base << ", Go-Back-N retransmit ["
					<< base << ".." << (nextSeq - 1) << "], new window=" << window
					<< " (" << retryRounds << "/" << RDT_MAX_RETRIES << ")" << endl;

				for (uint32_t i = base; i < nextSeq; i++) {
					if (!shouldSimulateLoss()) {
						vector<char> serializedPkt = getSerializedPacket(i);
						int sent = sendto(s, serializedPkt.data(), (int)serializedPkt.size(), 0, (const sockaddr*)&dest, sizeof(dest));
						if (sent == SOCKET_ERROR) {
							int err = WSAGetLastError();
							if (err != WSAECONNRESET) {
								cerr << "[RDT] GBN sendto failed: " << err << endl;
								return false;
							}
						}
					}
				}
				timerStart = chr::steady_clock::now();
				timerRunning = true;
			}
		}
	}

	// Gửi FIN → chờ FIN-ACK
	{
		RdtPacket finPkt;
		finPkt.seqNum = totalChunks;
		finPkt.flags = FLAG_FIN;
		finPkt.checksum = 0;
		finPkt.payloadLength = 0;
		vector<char> rawFin = serializePacket(finPkt);

		int timeout = RDT_TIMEOUT_MS;
		setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

		bool finAcked = false;
		for (int retry = 0; retry < RDT_MAX_RETRIES; retry++) {
			if (!shouldSimulateLoss()) {
				int sent = sendto(s, rawFin.data(), (int)rawFin.size(), 0, (const sockaddr*)&dest, sizeof(dest));
				if (sent == SOCKET_ERROR) {
					int err = WSAGetLastError();
					if (err != WSAECONNRESET) return false;
				}
			}
			else cout << "[RDT-SIM] Dropped outgoing FIN packet seq=" << totalChunks << endl;

			char ackBuf[RDT_HEADER_SIZE + 64];
			sockaddr_in ackFrom = {};
			int ackFromLen = sizeof(ackFrom);
			int ackLen = recvfrom(s, ackBuf, sizeof(ackBuf), 0, (sockaddr*)&ackFrom, &ackFromLen);

			if (ackLen == SOCKET_ERROR) {
				int err = WSAGetLastError();
				if (err == WSAECONNRESET) {
					std::this_thread::sleep_for(std::chrono::milliseconds(50));
					continue;
				}
				if (err == WSAETIMEDOUT) {
					cout << "[RDT] Timeout waiting for FIN-ACK, retransmit (" << (retry + 1) << "/" << RDT_MAX_RETRIES << ")" << endl;
					continue;
				}
				return false;
			}
			if (shouldSimulateLoss()) { cout << "[RDT-SIM] Dropped incoming FIN-ACK" << endl; continue; }

			if (ackFrom.sin_addr.s_addr != dest.sin_addr.s_addr ||
				ackFrom.sin_port != dest.sin_port) {
				continue;
			}

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

// =====================================================================
// rdtReceive — NHẬN DỮ LIỆU TIN CẬY QUA GO-BACK-N (BÊN NHẬN)
//   Logic giống Client: nhận gói DATA theo thứ tự, gửi cumulative ACK,
//   kết thúc khi nhận FIN đúng thứ tự → gửi FIN-ACK.
// =====================================================================
int DataChannel::rdtReceive(SOCKET s, std::vector<char>& outData, sockaddr_in& senderAddr) {
	uint32_t expectedSeq = 0;
	outData.clear();

	// "Học" sender từ gói đầu tiên nhận được
	bool senderLearned = false;
	sockaddr_in learnedSender = {};

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
			if (err == WSAECONNRESET) {
				continue;
			}
			return -1;
		}
		if (byteRecv == 0) continue;

		if (shouldSimulateLoss()) {
			cout << "[RDT-SIM] Dropped incoming packet (" << byteRecv << " bytes)" << endl;
			continue;
		}

		// Học sender address
		if (!senderLearned) {
			learnedSender = senderAddr;
			senderLearned = true;
		} else {
			if (senderAddr.sin_addr.s_addr != learnedSender.sin_addr.s_addr ||
				senderAddr.sin_port != learnedSender.sin_port) {
				continue;
			}
		}

		RdtPacket pkt;
		if (!deserializePacket(recvBuf, byteRecv, pkt)) {
			cout << "[RDT] Received corrupted packet, dropping (no ACK)" << endl;
			continue;
		}

		// Xử lý FIN
		if (pkt.flags & FLAG_FIN) {
			if (pkt.seqNum == expectedSeq) {
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
				cout << "[RDT] Premature/duplicate FIN (seq=" << pkt.seqNum << ", expected=" << expectedSeq << "), ignoring" << endl;
				continue;
			}
		}

		// Xử lý DATA
		if (pkt.flags & FLAG_DATA) {
			if (pkt.seqNum == expectedSeq) {
				outData.insert(outData.end(), pkt.payload.begin(), pkt.payload.end());
				expectedSeq++;
			}
			else if (pkt.seqNum < expectedSeq)
				cout << "[RDT] Duplicate DATA seq=" << pkt.seqNum
					<< " (already have up to " << (expectedSeq - 1) << "), re-ACK" << endl;

			else
				cout << "[RDT] Out-of-order DATA seq=" << pkt.seqNum
					<< " (expected " << expectedSeq << "), discarding (Go-Back-N)" << endl;


			// Gửi cumulative ACK — 0xFFFFFFFF nếu chưa nhận gói nào
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

// Nhận file qua RDT, ghi vào filepath
// Khác với Client: hỗ trợ kiểm tra ký tự cuối file khi append (APPE)
// Nếu file hiện có không kết thúc bằng '\n' → chèn thêm '\n' trước khi nối
bool DataChannel::receiveFile(const string& filepath, bool append, bool isAscii) {
	bool needsNewline = false;
	// Kiểm tra ký tự cuối file hiện có khi append ở chế độ ASCII
	if (append && isAscii) {
		ifstream check(filepath, ios::binary);
		if (check.is_open()) {
			check.seekg(0, ios::end);
			if (check.tellg() > 0) {
				check.seekg(-1, ios::end);  // Seek đến byte cuối cùng
				char lastChar;
				check.read(&lastChar, 1);
				if (lastChar != '\n') {
					needsNewline = true;  // Cần thêm '\n' trước dữ liệu mới
				}
			}
			check.close();
		}
	}

	ios::openmode mode = (append ? ios::app : ios::trunc);
	if (!isAscii) mode |= ios::binary;
	ofstream out(filepath, mode);
	if (!out.is_open()) {
		cerr << format("550 File unavailable, cannot open '{}' for writing", filepath) << endl;
		return false;
	}

	// Chèn newline nếu cần
	if (needsNewline) {
		out.write("\n", 1);
	}

	SOCKET s = udpSocket.load();
	if (s == INVALID_SOCKET) { out.close(); return false; }

	vector<char> fileData;
	sockaddr_in senderAddr = {};
	int totalRecv = rdtReceive(s, fileData, senderAddr);

	if (totalRecv < 0) {
		cerr << "426 Connection closed, transfer aborted" << endl;
		out.close();
		return false;
	}

	if (!fileData.empty()) out.write(fileData.data(), fileData.size());

	out.close();
	return true;
}

// Gửi file đến destIp:destPort qua RDT
bool DataChannel::sendFile(const string& filepath, const string& destIp, unsigned short destPort, bool isAscii) {
	ios::openmode mode = ios::in;
	if (!isAscii) mode |= ios::binary;
	ifstream in(filepath, mode);
	if (!in.is_open()) {
		cerr << format("550 File unavailable, cannot open '{}' for reading", filepath) << endl;
		return false;
	}

	sockaddr_in destAddr = {};
	destAddr.sin_family = AF_INET;
	destAddr.sin_port = htons(destPort);
	inet_pton(AF_INET, destIp.c_str(), &destAddr.sin_addr);

	in.seekg(0, ios::end);
	uintmax_t totalSize = in.tellg();
	in.seekg(0, ios::beg);

	SOCKET s = udpSocket.load();
	if (s == INVALID_SOCKET) return false;

	return rdtSend(s, in, totalSize, destAddr);
}

// Gửi file trong Passive mode: chờ probe từ Client → học địa chỉ → gửi file
bool DataChannel::sendFileAfterHandshake(const string& filepath, bool isAscii) {
	SOCKET s = udpSocket.load();
	if (s == INVALID_SOCKET) return false;

	// Chờ nhận probe (gói RDT nhỏ chứa 1 byte 'R') từ Client
	vector<char> probeData;
	sockaddr_in clientAddr = {};
	int probeLen = rdtReceive(s, probeData, clientAddr);

	if (probeLen < 0) {
		cerr << "426 Connection closed, transfer aborted (probe failed)" << endl;
		return false;
	}

	// Chuyển địa chỉ nhị phân → chuỗi IP
	char ipStr[INET_ADDRSTRLEN] = {0};
	inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, INET_ADDRSTRLEN);
	unsigned short learnedPort = ntohs(clientAddr.sin_port);

	return sendFile(filepath, ipStr, learnedPort, isAscii);
}

// Gửi gói probe nhỏ — dùng cho Active mode
bool DataChannel::sendProbe(const string& destIp, unsigned short destPort) {
	SOCKET s = udpSocket.load();
	if (s == INVALID_SOCKET) return false;

	sockaddr_in destAddr = {};
	destAddr.sin_family = AF_INET;
	destAddr.sin_port = htons(destPort);
	inet_pton(AF_INET, destIp.c_str(), &destAddr.sin_addr);

	const char probe = 'R';
	return rdtSend(s, &probe, 1, destAddr);
}

// Đóng socket UDP — dùng atomic exchange để đảm bảo chỉ đóng 1 lần
void DataChannel::stop() {
	SOCKET s = udpSocket.exchange(INVALID_SOCKET);
	if (s != INVALID_SOCKET) {
		std::this_thread::sleep_for(std::chrono::milliseconds(50));  // Chờ các thao tác IO kết thúc
		closesocket(s);
	}
}