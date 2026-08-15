// ======================================================================
// DataChannel.cpp — CÀI ĐẶT KÊNH DỮ LIỆU (UDP) CỦA CLIENT
//    Triển khai truyền file tin cậy qua UDP bằng giao thức RDT tự xây dựng:
//    - rdtSend: gửi dữ liệu bằng Go-Back-N Sliding Window + AIMD
//    - rdtReceive: nhận dữ liệu với cumulative ACK
//    - sendFile/receiveFile: giao diện cấp cao đọc/ghi file
//    - sendProbe/sendFileAfterHandshake: cơ chế bắt tay Passive mode
//    - Thanh tiến trình hiển thị trên dòng đầu console (hàng 0)
// ======================================================================
#include "DataChannel.h"

// Hiển thị thanh tiến trình (progress bar) trên DÒNG ĐẦU TIÊN của console (hàng 0).
// Dùng Windows Console API để di chuyển con trỏ đến vị trí (0,0), ghi text, rồi khôi phục
// vị trí con trỏ cũ — không làm gián đoạn output đang diễn ra ở vùng khác.
// Khi tiến trình đạt 100%, tạo một thread chờ 1 giây rồi xóa dòng tiến trình.
// Cơ chế epoch đảm bảo không xóa nhầm nếu một transfer mới bắt đầu trong khoảng 1s đó.
void updateProgressTitle(const string& action, uintmax_t current, uintmax_t total) {
    if (total <= 0) return;
    int percent = (int)((current * 100) / total);
    percent = max(0, min(100, percent));

    // Tạo thanh tiến trình dạng [##########] — mỗi # = 10%
    int filled = percent / 10;
    string bar = string(filled, '#') + string(10 - filled, '-');
    string text = format("[{}] [{}] {}% ({}/{} bytes)", action, bar, percent, current, total);

    // Dùng Console API để ghi text lên dòng đầu mà không cuộn console
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);  // Lấy handle của stdout console
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(h, &csbi)) {
        COORD oldPos = csbi.dwCursorPosition;   // Lưu vị trí con trỏ hiện tại
        SetConsoleCursorPosition(h, { 0, 0 });  // Di chuyển con trỏ về đầu dòng đầu tiên
        // Tính số khoảng trắng cần padding để ghi đè hết nội dung cũ trên dòng
        int padLen = csbi.dwSize.X - (int)text.length() - 1;
        if (padLen > 0) cout << text << string(padLen, ' ');
        else cout << text;
        SetConsoleCursorPosition(h, oldPos);    // Khôi phục vị trí con trỏ cũ
    }

    // Cơ chế epoch: mỗi lần gọi updateProgressTitle tăng epoch lên 1
    // Thread xóa thanh tiến trình chỉ thực hiện nếu epoch không thay đổi trong 1 giây
    // → tránh xóa nhầm thanh tiến trình của transfer mới bắt đầu ngay sau transfer cũ
    static atomic<int> progressEpoch{ 0 };
    int currentEpoch = ++progressEpoch;

    // Khi đạt 100% → tạo thread chờ 1s rồi xóa dòng tiến trình
    if (current == total) {
        thread([currentEpoch]() {
            std::this_thread::sleep_for(chr::seconds(1));
            // Chỉ xóa nếu không có transfer mới nào bắt đầu (epoch vẫn giữ nguyên)
            if (progressEpoch.load() == currentEpoch) {
                HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
                CONSOLE_SCREEN_BUFFER_INFO csbi;
                if (GetConsoleScreenBufferInfo(h, &csbi)) {
                    COORD oldPos = csbi.dwCursorPosition;
                    SetConsoleCursorPosition(h, { 0, 0 });
                    cout << string(csbi.dwSize.X - 1, ' ');  // Ghi đè dòng bằng khoảng trắng
                    SetConsoleCursorPosition(h, oldPos);
                }
            }
        }).detach();
    }
}

// Constructor: khởi tạo DataChannel với port dự kiến bind
// port = 0 → để OS tự chọn port ngẫu nhiên (thường dùng khi Client gửi file trong Passive mode)
DataChannel::DataChannel(unsigned short port) {
	this->udpPort = port;
	this->udpSocket.store(INVALID_SOCKET);
}

// Lấy port thực tế mà socket đã bind (hữu ích khi udpPort=0 và OS tự chọn port)
// Dùng getsockname() để truy vấn thông tin socket local
unsigned short DataChannel::getBoundPort() const {
	SOCKET s = udpSocket.load();
	if (s == INVALID_SOCKET) return 0;
	sockaddr_in local = {};
	int len = sizeof(local);
	if (getsockname(s, (sockaddr*)&local, &len) == SOCKET_ERROR) return 0;
	return ntohs(local.sin_port);  // ntohs: chuyển port từ network byte order → host byte order
}

// Tạo socket UDP và bind vào cổng udpPort
bool DataChannel::start() {
	// Tạo socket UDP (SOCK_DGRAM, IPPROTO_UDP)
	SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (s == INVALID_SOCKET) {
		cerr << format("425 Can't open data connection, cannot create socket (WSA error: {})", WSAGetLastError()) << endl;
		return false;
	}

	// Cho phép tái sử dụng địa chỉ/port — tránh lỗi "address already in use" khi bind lại nhanh
	BOOL reuse = TRUE;
	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

	// Cấu hình địa chỉ để bind: lắng nghe trên mọi interface (INADDR_ANY), cổng udpPort
	sockaddr_in serverAddrUdp = {};
	serverAddrUdp.sin_family = AF_INET;
	serverAddrUdp.sin_addr.s_addr = INADDR_ANY;
	serverAddrUdp.sin_port = htons(this->udpPort);  // htons: host → network byte order cho port

	if (bind(s, (sockaddr*)&serverAddrUdp, sizeof(serverAddrUdp)) == SOCKET_ERROR) {
		cerr << format("425 Can't open data connection, bind failed (WSA error: {})", WSAGetLastError()) << endl;
		closesocket(s);
		return false;
	}

	this->udpSocket.store(s);  // Lưu socket vào biến atomic — an toàn cho đa luồng
	return true;
}

// =====================================================================
// rdtSend (phiên bản buffer) — GỬI DỮ LIỆU TIN CẬY QUA UDP BẰNG GO-BACK-N
//   Chia dữ liệu thành các chunk ≤ RDT_MAX_PAYLOAD byte, đóng gói thành
//   RdtPacket, rồi gửi theo cơ chế Go-Back-N Sliding Window:
//   - Gửi liên tục các gói trong cửa sổ [base, base+window)
//   - Chờ ACK tích lũy (cumulative ACK): ACK(n) xác nhận đã nhận tất cả gói ≤ n
//   - Timeout → retransmit toàn bộ cửa sổ (Go-Back-N)
//   - AIMD: nhận ACK thành công → window++ (Additive Increase),
//           timeout → window/=2 (Multiplicative Decrease)
//   - Kết thúc bằng gói FIN → chờ FIN-ACK
// =====================================================================
bool DataChannel::rdtSend(SOCKET s, const char* data, size_t len, const sockaddr_in& dest, uintmax_t totalSize) {
	// Tính tổng số chunk cần gửi (làm tròn lên)
	uint32_t totalChunks = (len == 0) ? 0 : (uint32_t)((len + RDT_MAX_PAYLOAD - 1) / RDT_MAX_PAYLOAD);
	// Pre-serialize tất cả gói tin DATA vào mảng — tránh serialize lại khi retransmit
	vector<vector<char>> serializedPkts(totalChunks);
	for (uint32_t i = 0; i < totalChunks; i++) {
		size_t offset = (size_t)i * RDT_MAX_PAYLOAD;
		size_t chunkSize = min((size_t)RDT_MAX_PAYLOAD, len - offset);

		RdtPacket pkt;
		pkt.seqNum = i;              // Sequence number = chỉ số chunk (đánh số từ 0)
		pkt.flags = FLAG_DATA;
		pkt.checksum = 0;            // Sẽ được tính trong serializePacket
		pkt.payloadLength = (uint16_t)chunkSize;
		pkt.payload.assign(data + offset, data + offset + chunkSize);
		serializedPkts[i] = serializePacket(pkt);
	}

	// Đặt timeout nhận ACK = RDT_POLL_MS (50ms) — polling nhanh để kiểm tra ACK thường xuyên
	int pollTimeout = RDT_POLL_MS;
	setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&pollTimeout, sizeof(pollTimeout));

	uint32_t base = 0;        // base: chỉ số gói đầu tiên trong cửa sổ chưa được ACK
	uint32_t nextSeq = 0;     // nextSeq: chỉ số gói tiếp theo cần gửi
	int window = RDT_INITIAL_WINDOW; // Kích thước cửa sổ hiện tại (AIMD)
	int retryRounds = 0;      // Số lần đã retry (Go-Back-N retransmit)
	bool timerRunning = false; // Có đang đếm thời gian chờ ACK cho gói base không
	chr::steady_clock::time_point timerStart;  // Thời điểm bắt đầu timer
	auto lastPrintTime = chr::steady_clock::now();  // Để giới hạn tần suất cập nhật thanh tiến trình

	// === Vòng lặp chính Go-Back-N: gửi cho đến khi tất cả gói được ACK ===
	while (base < totalChunks) {
		// --- Gửi các gói mới trong phạm vi cửa sổ [base, base+window) ---
		while (nextSeq < totalChunks && nextSeq < base + (uint32_t)window) {
			if (!shouldSimulateLoss()) {
				int sent = sendto(s, serializedPkts[nextSeq].data(), (int)serializedPkts[nextSeq].size(), 0,
					(const sockaddr*)&dest, sizeof(dest));
				if (sent == SOCKET_ERROR) {
					int err = WSAGetLastError();
					// Bỏ qua lỗi tạm thời (CONNRESET, NOTSOCK, EINTR)
					if (err != WSAECONNRESET && err != WSAENOTSOCK && err != WSAEINTR) {
						cerr << "[RDT] sendto failed with error: " << err << endl;
					}
					return false;
				}
			}
			else cout << "[RDT-SIM] Dropped outgoing DATA packet seq=" << nextSeq << endl;
			nextSeq++;
		}

		// --- Bắt đầu timer khi có gói đã gửi nhưng chưa được ACK ---
		if (!timerRunning && base < nextSeq) {
			timerStart = chr::steady_clock::now();
			timerRunning = true;
		}

		// --- Chờ nhận ACK (với timeout = RDT_POLL_MS) ---
		char ackBuf[RDT_HEADER_SIZE + 64];
		sockaddr_in ackFrom = {};
		int ackFromLen = sizeof(ackFrom);
		int ackLen = recvfrom(s, ackBuf, sizeof(ackBuf), 0, (sockaddr*)&ackFrom, &ackFromLen);

		if (ackLen == SOCKET_ERROR) {
			int err = WSAGetLastError();
			// WSAECONNRESET: đầu bên kia đóng socket → chờ thêm
			if (err == WSAECONNRESET) {
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
				continue;
			}
			// WSAETIMEDOUT: hết thời gian chờ → kiểm tra timer bên dưới
			if (err != WSAETIMEDOUT) return false;
		}
		// Giả lập mất gói ACK đến
		else if (shouldSimulateLoss()) cout << "[RDT-SIM] Dropped incoming ACK" << endl;

		else {
			// Kiểm tra ACK đến từ đúng địa chỉ đích — bỏ qua gói từ nguồn khác
			if (ackFrom.sin_addr.s_addr != dest.sin_addr.s_addr ||
				ackFrom.sin_port != dest.sin_port) {
				continue;
			}

			RdtPacket ackPkt;
			if (deserializePacket(ackBuf, ackLen, ackPkt) && (ackPkt.flags & FLAG_ACK)) {
				// Cumulative ACK: ACK(n) nghĩa là đã nhận tất cả gói có seq ≤ n
				// 0xFFFFFFFF là giá trị đặc biệt: chưa nhận được gói nào (ACK trước gói 0)
				if (ackPkt.seqNum != 0xFFFFFFFFu && ackPkt.seqNum + 1 > base) {
					base = ackPkt.seqNum + 1;  // Dịch cửa sổ sang phải
					window = min(window + 1, RDT_MAX_WINDOW);  // Additive Increase: tăng cửa sổ +1
					retryRounds = 0;           // Reset bộ đếm retry
					timerRunning = false;       // Tắt timer, sẽ bật lại vòng lặp tiếp
				}
			}
		}

		// --- Kiểm tra timeout: nếu quá RDT_TIMEOUT_MS mà chưa nhận ACK → Go-Back-N retransmit ---
		if (timerRunning) {
			auto elapsedMs = chr::duration_cast<chr::milliseconds>(chr::steady_clock::now() - timerStart).count();
			if (elapsedMs >= RDT_TIMEOUT_MS) {
				retryRounds++;
				if (retryRounds > RDT_MAX_RETRIES) {
					cerr << format("[RDT] Max Go-Back-N retries reached at base={}, transfer failed", base) << endl;
					return false;
				}
				// Multiplicative Decrease: giảm cửa sổ còn 1/2 (tối thiểu = RDT_MIN_WINDOW)
				window = max(window / 2, RDT_MIN_WINDOW);
				cout << "[RDT] Timeout on base seq=" << base << ", Go-Back-N retransmit ["
					<< base << ".." << (nextSeq - 1) << "], new window=" << window
					<< " (" << retryRounds << "/" << RDT_MAX_RETRIES << ")" << endl;

				// Retransmit toàn bộ cửa sổ: gửi lại tất cả gói từ base đến nextSeq-1
				for (uint32_t i = base; i < nextSeq; i++) {
					if (!shouldSimulateLoss()) {
						int sent = sendto(s, serializedPkts[i].data(), (int)serializedPkts[i].size(), 0, (const sockaddr*)&dest, sizeof(dest));
						if (sent == SOCKET_ERROR) {
							int err = WSAGetLastError();
							if (err != WSAECONNRESET && err != WSAENOTSOCK && err != WSAEINTR) {
								cerr << "[RDT] GBN sendto failed: " << err << endl;
							}
							return false;
						}
					}
				}
				timerStart = chr::steady_clock::now();  // Reset timer
				timerRunning = true;
			}
		}

		// --- Cập nhật thanh tiến trình mỗi 500ms ---
		auto now = chr::steady_clock::now();
		if (chr::duration_cast<chr::milliseconds>(now - lastPrintTime).count() >= 500) {
            if (len > 0) {
                uintmax_t transferred = min((uintmax_t)(base * RDT_MAX_PAYLOAD), (uintmax_t)len);
                updateProgressTitle("Uploading", transferred, len);
            }
			lastPrintTime = now;
		}
	}

	// === Gửi gói FIN để báo kết thúc truyền → chờ FIN-ACK ===
	{
		RdtPacket finPkt;
		finPkt.seqNum = totalChunks;  // seqNum của FIN = tổng số chunk (sau chunk cuối cùng)
		finPkt.flags = FLAG_FIN;
		finPkt.checksum = 0;
		finPkt.payloadLength = 0;
		vector<char> rawFin = serializePacket(finPkt);

		// Đặt timeout dài hơn cho việc chờ FIN-ACK
		int timeout = RDT_TIMEOUT_MS;
		setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

		bool finAcked = false;
		for (int retry = 0; retry < RDT_MAX_RETRIES; retry++) {
			// Gửi gói FIN
			if (!shouldSimulateLoss()) {
				int sent = sendto(s, rawFin.data(), (int)rawFin.size(), 0, (const sockaddr*)&dest, sizeof(dest));
				if (sent == SOCKET_ERROR) {
					int err = WSAGetLastError();
					if (err != WSAECONNRESET) return false;
				}
			}
			else cout << "[RDT-SIM] Dropped outgoing FIN packet seq=" << totalChunks << endl;

			// Chờ nhận FIN-ACK
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

			// Kiểm tra FIN-ACK đến từ đúng địa chỉ đích
			if (ackFrom.sin_addr.s_addr != dest.sin_addr.s_addr ||
				ackFrom.sin_port != dest.sin_port) {
				continue;
			}

			RdtPacket ackPkt;
			if (!deserializePacket(ackBuf, ackLen, ackPkt)) continue;
			// FIN-ACK hợp lệ: có cờ ACK và seqNum khớp với seqNum của gói FIN
			if ((ackPkt.flags & FLAG_ACK) && ackPkt.seqNum == totalChunks) { finAcked = true; break; }
		}

		if (!finAcked) {
			cerr << "[RDT] Max retries reached for FIN, transfer failed" << endl;
			return false;
		}
	}

	// Cập nhật thanh tiến trình lên 100% khi hoàn tất
	if (len > 0) updateProgressTitle("Uploading", len, len);
	return true;
}

// =====================================================================
// rdtSend (phiên bản ifstream) — GỬI FILE LỚN QUA RDT
//   Giống phiên bản buffer nhưng đọc dữ liệu trực tiếp từ file stream
//   thay vì từ buffer đã có sẵn trong bộ nhớ. Dùng lambda getSerializedPacket
//   để đọc chunk từ file theo offset mỗi khi cần gửi hoặc retransmit —
//   tiết kiệm bộ nhớ cho file lớn (không cần nạp toàn bộ file vào RAM).
// =====================================================================
bool DataChannel::rdtSend(SOCKET s, std::ifstream& in, uintmax_t len, const sockaddr_in& dest, uintmax_t totalSize) {
	uint32_t totalChunks = (len == 0) ? 0 : (uint32_t)((len + RDT_MAX_PAYLOAD - 1) / RDT_MAX_PAYLOAD);

	// Lambda đọc chunk từ file theo seq number → serialize thành gói RDT
	// Gọi mỗi khi cần gửi lần đầu hoặc retransmit — đọc file theo offset (seekg)
	auto getSerializedPacket = [&](uint32_t seq) -> vector<char> {
		size_t offset = (size_t)seq * RDT_MAX_PAYLOAD;
		size_t chunkSize = min((size_t)RDT_MAX_PAYLOAD, (size_t)(len - offset));

		RdtPacket pkt;
		pkt.seqNum = seq;
		pkt.flags = FLAG_DATA;
		pkt.checksum = 0;
		pkt.payloadLength = (uint16_t)chunkSize;
		pkt.payload.resize(chunkSize);

		in.clear();                        // Xóa cờ lỗi/EOF nếu có
		in.seekg(offset, ios::beg);        // Seek đến vị trí offset trong file
		in.read(pkt.payload.data(), chunkSize);  // Đọc chunkSize byte

		return serializePacket(pkt);
	};

	// Cấu hình giống phiên bản buffer — Go-Back-N + AIMD
	int pollTimeout = RDT_POLL_MS;
	setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&pollTimeout, sizeof(pollTimeout));

	uint32_t base = 0;
	uint32_t nextSeq = 0;
	int window = RDT_INITIAL_WINDOW;
	int retryRounds = 0;
	bool timerRunning = false;
	chr::steady_clock::time_point timerStart;
	auto lastPrintTime = chr::steady_clock::now();

	// === Vòng lặp Go-Back-N (logic tương tự phiên bản buffer) ===
	while (base < totalChunks) {
		// Gửi các gói mới trong phạm vi cửa sổ
		while (nextSeq < totalChunks && nextSeq < base + (uint32_t)window) {
			if (!shouldSimulateLoss()) {
				vector<char> serializedPkt = getSerializedPacket(nextSeq);
				int sent = sendto(s, serializedPkt.data(), (int)serializedPkt.size(), 0,
					(const sockaddr*)&dest, sizeof(dest));
				if (sent == SOCKET_ERROR) {
					int err = WSAGetLastError();
					if (err != WSAECONNRESET && err != WSAENOTSOCK && err != WSAEINTR) {
						cerr << "[RDT] sendto failed with error: " << err << endl;
					}
					return false;
				}
			}
			else cout << "[RDT-SIM] Dropped outgoing DATA packet seq=" << nextSeq << endl;
			nextSeq++;
		}

		if (!timerRunning && base < nextSeq) {
			timerStart = chr::steady_clock::now();
			timerRunning = true;
		}

		// Chờ ACK
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
					SetConsoleTitleA("FTP Client");
					return false;
				}
				window = max(window / 2, RDT_MIN_WINDOW);  // Multiplicative Decrease
				cout << "[RDT] Timeout on base seq=" << base << ", Go-Back-N retransmit ["
					<< base << ".." << (nextSeq - 1) << "], new window=" << window
					<< " (" << retryRounds << "/" << RDT_MAX_RETRIES << ")" << endl;

				// Retransmit: đọc lại từ file và gửi lại toàn bộ cửa sổ
				for (uint32_t i = base; i < nextSeq; i++) {
					if (!shouldSimulateLoss()) {
						vector<char> serializedPkt = getSerializedPacket(i);
						int sent = sendto(s, serializedPkt.data(), (int)serializedPkt.size(), 0, (const sockaddr*)&dest, sizeof(dest));
						if (sent == SOCKET_ERROR) {
							int err = WSAGetLastError();
							if (err != WSAECONNRESET && err != WSAENOTSOCK && err != WSAEINTR) {
								cerr << "[RDT] GBN sendto failed: " << err << endl;
							}
							return false;
						}
					}
				}
				timerStart = chr::steady_clock::now();
				timerRunning = true;
			}
		}

		// Cập nhật thanh tiến trình mỗi 500ms
		auto now = chr::steady_clock::now();
		if (chr::duration_cast<chr::milliseconds>(now - lastPrintTime).count() >= 500) {
			if (totalSize > 0) {
				uintmax_t transferred = min((uintmax_t)(base * RDT_MAX_PAYLOAD), (uintmax_t)totalSize);
				updateProgressTitle("Uploading", transferred, totalSize);
			}
			lastPrintTime = now;
		}
	}

	// === Gửi FIN → chờ FIN-ACK (giống phiên bản buffer) ===
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

	if (totalSize > 0) updateProgressTitle("Uploading", totalSize, totalSize);
	return true;
}

// =====================================================================
// rdtReceive — NHẬN DỮ LIỆU TIN CẬY QUA UDP BẰNG GO-BACK-N (BÊN NHẬN)
//   Nhận các gói DATA theo thứ tự sequence number, gửi cumulative ACK:
//   - Gói đúng thứ tự (seqNum == expectedSeq) → lưu payload, tăng expectedSeq, ACK(expectedSeq-1)
//   - Gói duplicate (seqNum < expectedSeq) → gửi lại ACK(expectedSeq-1) để nhắc bên gửi
//   - Gói sai thứ tự (seqNum > expectedSeq) → bỏ qua (theo Go-Back-N), ACK(expectedSeq-1)
//   - Nhận gói FIN với seqNum đúng → gửi FIN-ACK, kết thúc
//   Giá trị ACK đặc biệt: 0xFFFFFFFF = chưa nhận thành công gói nào
// =====================================================================
int DataChannel::rdtReceive(SOCKET s, std::vector<char>& outData, sockaddr_in& senderAddr, uintmax_t totalSize) {
	uint32_t expectedSeq = 0;  // Sequence number tiếp theo đang mong đợi
	outData.clear();

	// "Học" địa chỉ sender từ gói đầu tiên nhận được — sau đó chỉ chấp nhận gói từ sender này
	bool senderLearned = false;
	sockaddr_in learnedSender = {};

	// Timeout tổng thể: chờ tối đa = RDT_TIMEOUT_MS * (RDT_MAX_RETRIES + 1)
	// Nếu hết thời gian mà không nhận thêm gói nào → coi như sender đã ngắt kết nối
	int timeout = RDT_TIMEOUT_MS * (RDT_MAX_RETRIES + 1);
	setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

	char recvBuf[RDT_HEADER_SIZE + RDT_MAX_PAYLOAD + 64];
	int senderAddrLen = sizeof(senderAddr);
	auto lastPrintTime = chr::steady_clock::now();

	while (true) {
		// Nhận gói UDP
		int byteRecv = recvfrom(s, recvBuf, sizeof(recvBuf), 0,
			(sockaddr*)&senderAddr, &senderAddrLen);

		if (byteRecv == SOCKET_ERROR) {
			int err = WSAGetLastError();
			if (err == WSAETIMEDOUT) {
				cerr << "[RDT] Receiver timeout — sender may have disconnected" << endl;
				return -1;
			}
			if (err == WSAECONNRESET) {
				continue;  // Bỏ qua lỗi CONNRESET, thử nhận tiếp
			}
			return -1;
		}
		if (byteRecv == 0) continue;

		// Giả lập mất gói đến
		if (shouldSimulateLoss()) {
			cout << "[RDT-SIM] Dropped incoming packet (" << byteRecv << " bytes)" << endl;
			continue;
		}

		// Học địa chỉ sender từ gói đầu tiên, sau đó lọc bỏ gói từ nguồn khác
		if (!senderLearned) {
			learnedSender = senderAddr;
			senderLearned = true;
		} else {
			if (senderAddr.sin_addr.s_addr != learnedSender.sin_addr.s_addr ||
				senderAddr.sin_port != learnedSender.sin_port) {
				continue;  // Bỏ qua gói từ nguồn khác
			}
		}

		// Giải gói
		RdtPacket pkt;
		if (!deserializePacket(recvBuf, byteRecv, pkt)) {
			cout << "[RDT] Received corrupted packet, dropping (no ACK)" << endl;
			continue;  // Gói lỗi checksum → không gửi ACK (bên gửi sẽ timeout và retransmit)
		}

		// --- Xử lý gói FIN: kết thúc phiên truyền ---
		if (pkt.flags & FLAG_FIN) {
			if (pkt.seqNum == expectedSeq) {
				// FIN đúng thứ tự → gửi FIN-ACK và kết thúc
				RdtPacket ackPkt;
				ackPkt.seqNum = pkt.seqNum;  // ACK seqNum = seqNum của FIN
				ackPkt.flags = FLAG_ACK;
				ackPkt.checksum = 0;
				ackPkt.payloadLength = 0;
				std::vector<char> rawAck = serializePacket(ackPkt);
				sendto(s, rawAck.data(), (int)rawAck.size(), 0, (const sockaddr*)&senderAddr, sizeof(senderAddr));
				break;  // Thoát vòng lặp nhận
			}
			else {
				// FIN đến sớm hoặc trùng lặp → bỏ qua, chờ nhận đủ DATA trước
				cout << "[RDT] Premature/duplicate FIN (seq=" << pkt.seqNum << ", expected=" << expectedSeq << "), ignoring" << endl;
				continue;
			}
		}

		// --- Xử lý gói DATA ---
		if (pkt.flags & FLAG_DATA) {
			if (pkt.seqNum == expectedSeq) {
				// Gói đúng thứ tự → lưu payload vào buffer kết quả
				outData.insert(outData.end(), pkt.payload.begin(), pkt.payload.end());
				expectedSeq++;
			}
			else if (pkt.seqNum < expectedSeq)
				// Gói duplicate (đã nhận rồi) → gửi lại ACK để bên gửi biết
				cout << "[RDT] Duplicate DATA seq=" << pkt.seqNum
				<< " (already have up to " << (expectedSeq - 1) << "), re-ACK" << endl;

			else
				// Gói sai thứ tự (quá xa) → bỏ qua theo Go-Back-N (không buffer)
				cout << "[RDT] Out-of-order DATA seq=" << pkt.seqNum
				<< " (expected " << expectedSeq << "), discarding (Go-Back-N)" << endl;


			// Gửi cumulative ACK: xác nhận đã nhận tất cả gói đến expectedSeq-1
			// 0xFFFFFFFF: giá trị đặc biệt khi chưa nhận thành công gói nào (expectedSeq=0)
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

		// Cập nhật thanh tiến trình mỗi 500ms
		auto now = chr::steady_clock::now();
		if (chr::duration_cast<chr::milliseconds>(now - lastPrintTime).count() >= 500) {
            if (totalSize > 0) {
                updateProgressTitle("Downloading", outData.size(), totalSize);
            }
			lastPrintTime = now;
		}
	}

	if (totalSize > 0) updateProgressTitle("Downloading", totalSize, totalSize);
	return (int)outData.size();
}

// Nhận file từ đầu bên kia qua RDT, ghi vào filepath
// append: true = nối thêm vào file hiện có, false = ghi đè
// isAscii: true = mở file ở chế độ text (ASCII mode), false = binary mode
bool DataChannel::receiveFile(const string& filepath, uintmax_t totalSize, bool append, bool isAscii) {
	ios::openmode mode = (append ? ios::app : ios::trunc);
	if (!isAscii) mode |= ios::binary;  // Binary mode: không chuyển đổi ký tự đặc biệt
	ofstream out(filepath, mode);
	if (!out.is_open()) {
		cerr << format("550 File unavailable, cannot open '{}' for writing", filepath) << endl;
		return false;
	}

	SOCKET s = udpSocket.load();
	if (s == INVALID_SOCKET) { out.close(); return false; }

	// Nhận toàn bộ dữ liệu qua RDT vào buffer
	vector<char> fileData;
	sockaddr_in senderAddr = {};
	int totalRecv = rdtReceive(s, fileData, senderAddr, totalSize);

	if (totalRecv < 0) {
		cerr << "426 Connection closed, transfer aborted" << endl;  // 426: kết nối bị đóng, truyền bị hủy
		out.close();
		return false;
	}

	// Ghi toàn bộ dữ liệu đã nhận vào file
	if (!fileData.empty()) out.write(fileData.data(), fileData.size());

	out.close();
	return true;
}

// Gửi file đến địa chỉ destIp:destPort qua RDT
// Đọc file bằng ifstream, lấy kích thước thực, rồi gọi rdtSend (phiên bản ifstream)
bool DataChannel::sendFile(const string& filepath, const string& destIp, unsigned short destPort, uintmax_t totalSize, bool isAscii) {
	ios::openmode mode = ios::in;
	if (!isAscii) mode |= ios::binary;
	ifstream in(filepath, mode);
	if (!in.is_open()) {
		cerr << format("550 File unavailable, cannot open '{}' for reading", filepath) << endl;
		return false;
	}

	// Cấu hình địa chỉ đích
	sockaddr_in destAddr = {};
	destAddr.sin_family = AF_INET;
	destAddr.sin_port = htons(destPort);                          // Chuyển port sang network byte order
	inet_pton(AF_INET, destIp.c_str(), &destAddr.sin_addr);      // Chuyển IP string → binary

	// Lấy kích thước file thực tế
	in.seekg(0, ios::end);
	uintmax_t actualSize = in.tellg();
	in.seekg(0, ios::beg);

	SOCKET s = udpSocket.load();
	if (s == INVALID_SOCKET) return false;

	// Gọi rdtSend phiên bản ifstream — totalSize dùng cho thanh tiến trình
	return rdtSend(s, in, actualSize, destAddr, totalSize > 0 ? totalSize : actualSize);
}

// Gửi file trong Passive mode: chờ nhận probe từ Client trước để "học" địa chỉ Client
// (vì trong Passive mode, Server không biết trước Client sẽ gửi từ port/IP nào)
// Sau khi nhận probe → biết được clientAddr → gọi sendFile bình thường
bool DataChannel::sendFileAfterHandshake(const string& filepath, uintmax_t totalSize, bool isAscii) {
	SOCKET s = udpSocket.load();
	if (s == INVALID_SOCKET) return false;

	// Nhận probe (gói RDT nhỏ chứa 1 byte 'R') từ Client
	vector<char> probeData;
	sockaddr_in clientAddr = {};
	int probeLen = rdtReceive(s, probeData, clientAddr);

	if (probeLen < 0) {
		cerr << "426 Connection closed, transfer aborted (probe failed)" << endl;
		return false;
	}

	// Chuyển đổi địa chỉ nhị phân → chuỗi IP
	char ipStr[INET_ADDRSTRLEN] = {0};
	inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, INET_ADDRSTRLEN);
	unsigned short learnedPort = ntohs(clientAddr.sin_port);  // Chuyển port về host byte order

	// Gửi file đến địa chỉ Client vừa học được
	return sendFile(filepath, ipStr, learnedPort, totalSize, isAscii);
}

// Gửi gói probe nhỏ (1 byte 'R') đến Server — dùng trong Passive mode
// Mục đích: cho Server biết địa chỉ IP:port của Client để Server gửi file ngược lại
// Probe được gửi qua rdtSend bình thường (có cơ chế tin cậy) nên đảm bảo đến nơi
bool DataChannel::sendProbe(const string& destIp, unsigned short destPort) {
	SOCKET s = udpSocket.load();
	if (s == INVALID_SOCKET) return false;

	sockaddr_in destAddr = {};
	destAddr.sin_family = AF_INET;
	destAddr.sin_port = htons(destPort);
	inet_pton(AF_INET, destIp.c_str(), &destAddr.sin_addr);

	const char probe = 'R';  // Ký tự probe tượng trưng
	return rdtSend(s, &probe, 1, destAddr);
}

// Đóng socket UDP, dừng kênh dữ liệu
// Dùng atomic exchange để đảm bảo chỉ đóng socket 1 lần dù gọi từ nhiều thread
// Sleep 50ms trước khi closesocket: cho phép các thao tác recvfrom/sendto đang chờ kịp kết thúc
void DataChannel::stop() {
	SOCKET s = udpSocket.exchange(INVALID_SOCKET);  // Atomic: đổi giá trị và lấy giá trị cũ
	if (s != INVALID_SOCKET) {
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		closesocket(s);
	}
}