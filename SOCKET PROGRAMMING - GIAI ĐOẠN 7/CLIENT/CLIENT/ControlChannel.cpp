// ======================================================================
// ControlChannel.cpp — CÀI ĐẶT KÊNH ĐIỀU KHIỂN (TCP) CỦA CLIENT
//    Quản lý toàn bộ giao tiếp TCP với Server:
//    - start(): tạo socket TCP, connect đến Server
//    - run(): vòng lặp chính nhận lệnh từ người dùng, gửi đến Server,
//             chờ phản hồi (đồng bộ hóa bằng condition_variable)
//    - receiverLoop(): thread phụ lắng nghe phản hồi từ Server liên tục,
//             phân tích mã trạng thái FTP, kích hoạt truyền dữ liệu
//    - doDataTransfer(): tạo DataChannel UDP, thực hiện gửi/nhận file
// ======================================================================
#include "ControlChannel.h"
#include "DataChannel.h"

// Constructor: lưu thông tin kết nối đến Server
ControlChannel::ControlChannel(unsigned short port, string IP) {
    this->serverTcpPort = port;
    this->serverIp = IP;
    this->tcpSocket = INVALID_SOCKET;
    this->currentDir = "/";  // Thư mục làm việc ban đầu = root
}

// Destructor: đảm bảo thread nhận phản hồi được dừng và join
ControlChannel::~ControlChannel() {
    keepRunning = false;
    if (receiverThread.joinable()) receiverThread.join();
}

// Tạo socket TCP và kết nối đến Server
bool ControlChannel::start() {
    // Tạo socket TCP (SOCK_STREAM, IPPROTO_TCP)
    this->tcpSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (this->tcpSocket == INVALID_SOCKET) {
        cerr << format("421 Service not available, cannot create socket (WSA error: {})", WSAGetLastError()) << endl;
        return false;
    }

    // Cấu hình địa chỉ Server để connect
    sockaddr_in serverAddr = {};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(this->serverTcpPort);  // Chuyển port sang network byte order

    // Chuyển đổi chuỗi IP → dạng nhị phân (inet_pton: presentation to network)
    if (inet_pton(AF_INET, (this->serverIp).c_str(), &serverAddr.sin_addr) <= 0) {
        cerr << "501 Syntax error in parameters, invalid IP address" << endl;
        closesocket(this->tcpSocket);
        this->tcpSocket = INVALID_SOCKET;
        return false;
    }

    // Kết nối TCP đến Server
    if (connect(this->tcpSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        cerr << format("421 Service not available, cannot connect to server (WSA error: {})", WSAGetLastError()) << endl;
        closesocket(this->tcpSocket);
        this->tcpSocket = INVALID_SOCKET;
        return false;
    }

    cout << "200 Connected successfully, ready for commands" << endl;  // 200: OK
    return true;
}

// Phân tích tham số lệnh PORT: "h1,h2,h3,h4,p1,p2"
// h1-h4: 4 octet của IP, p1-p2: tính port = p1*256 + p2
// Ví dụ: "127,0,0,1,4,1" → port = 4*256 + 1 = 1025
bool ControlChannel::parsePortArgLocal(const string& arg, unsigned short& outPort) {
    vector<int> nums;
    stringstream ss(arg);
    string token;
    while (getline(ss, token, ',')) {
        try { nums.push_back(stoi(token)); }
        catch (...) { return false; }
    }
    if (nums.size() != 6) return false;
    outPort = (unsigned short)(nums[4] * 256 + nums[5]);  // Port = p1*256 + p2
    return true;
}

// Phân tích phản hồi PASV (227) từ Server: "227 Entering Passive Mode (h1,h2,h3,h4,p1,p2)"
// Trích xuất 6 số trong ngoặc → tính port = p1*256 + p2
bool ControlChannel::parsePasvReply(const string& reply, unsigned short& outPort) {
    size_t open = reply.find('(');
    size_t close = reply.find(')');
    if (open == string::npos || close == string::npos || close <= open) return false;

    string inside = reply.substr(open + 1, close - open - 1);
    vector<int> nums;
    stringstream ss(inside);
    string token;
    while (getline(ss, token, ',')) {
        try { nums.push_back(stoi(token)); }
        catch (...) { return false; }
    }
    if (nums.size() != 6) return false;
    outPort = (unsigned short)(nums[4] * 256 + nums[5]);
    return true;
}

// Phân tích chuỗi "PORT=xxxxx" nhúng trong phản hồi 150 từ Server
// Server gắn port vào phản hồi 150 khi ở Active mode để Client biết port Server đang lắng nghe
bool ControlChannel::parseEmbeddedPort(const string& reply, unsigned short& outPort) {
    const string marker = "PORT=";
    size_t pos = reply.find(marker);
    if (pos == string::npos) return false;
    pos += marker.size();
    size_t end = pos;
    while (end < reply.size() && isdigit((unsigned char)reply[end])) end++;
    if (end == pos) return false;
    try { outPort = (unsigned short)stoi(reply.substr(pos, end - pos)); }
    catch (...) { return false; }
    return true;
}

// Phân giải đường dẫn tương đối → đường dẫn vật lý trong CLIENT_ROOT
// Đường dẫn bắt đầu bằng '/' = tuyệt đối, ngược lại = tương đối từ currentDir
fs::path ControlChannel::resolvePath(const string& arg) {
    fs::path logical = (!arg.empty() && arg[0] == '/')
        ? fs::path(arg)
        : fs::path(this->currentDir) / arg;

    // Chuẩn hóa đường dẫn (xử lý "..", "." ...) và chuyển sang dạng generic (dùng '/')
    string normStr = logical.lexically_normal().generic_string();
    if (normStr.empty()) normStr = "/";
    if (normStr[0] != '/') return fs::path();  // Đường dẫn không hợp lệ (thoát khỏi root)

    // Chuyển logical path → physical path trong CLIENT_ROOT
    fs::path relativePart = (normStr == "/") ? fs::path() : fs::path(normStr.substr(1));
    return fs::weakly_canonical(CLIENT_ROOT / relativePart);
}


// Thực hiện truyền dữ liệu qua kênh DataChannel (UDP) tùy theo lệnh:
// - STOR/STOU/APPE: gửi file từ Client lên Server
// - RETR: tải file từ Server về Client
// - LIST/NLST: nhận danh sách thư mục từ Server
// Tạo DataChannel tạm → gửi/nhận → đóng DataChannel
void ControlChannel::doDataTransfer(const string& cmdWord, const string& filename, uintmax_t totalSize) {
    string localPath = resolvePath(filename).string();

    // === Upload file: STOR (ghi đè), STOU (tên tự động), APPE (nối thêm) ===
    if (cmdWord == "STOR" || cmdWord == "STOU" || cmdWord == "APPE") {
        totalSize = fs::file_size(localPath);
        // Chọn port đích: Passive → dùng serverPasvPort, Active → dùng serverUploadPort (từ phản hồi 150)
        unsigned short destPort = (dataMode.load() == DataMode::PASSIVE) ? serverPasvPort.load() : serverUploadPort.load();
        DataChannel dc(0);  // Port = 0 → để OS tự chọn port
        activeDataChannel.store(&dc);  // Đăng ký để có thể ABOR
        if (dc.start()) {
            dc.sendFile(localPath, serverIp, destPort, totalSize, isAsciiMode.load());
            dc.stop();
        }
        activeDataChannel.store(nullptr);
    }
    // === Download file (RETR) hoặc nhận danh sách thư mục (LIST/NLST) ===
    else if (cmdWord == "RETR" || cmdWord == "LIST" || cmdWord == "NLST") {
        DataMode mode = dataMode.load();

        bool isList = (cmdWord == "LIST" || cmdWord == "NLST");
        string targetFile = localPath;
        // LIST/NLST: tạo file tạm để chứa kết quả, sau đó in ra console rồi xóa
        if (isList) {
            auto ms = chr::duration_cast<chr::milliseconds>(chr::system_clock::now().time_since_epoch()).count();
            string tempFileName = format(".tmp_list_{}.tmp", ms);
            targetFile = (CLIENT_ROOT / tempFileName).string();
            int counter = 0;
            while (fs::exists(targetFile)) {
                counter++;
                targetFile = (CLIENT_ROOT / format(".tmp_list_{}_{}.tmp", ms, counter)).string();
            }
        }

        if (mode == DataMode::PASSIVE) {
            // Passive mode: Client tạo DataChannel → gửi probe đến Server → nhận file
            DataChannel dc(0);
            activeDataChannel.store(&dc);
            if (dc.start()) {
                dc.sendProbe(serverIp, serverPasvPort.load());  // Gửi probe để Server biết địa chỉ Client
                dc.receiveFile(targetFile, totalSize, false, isAsciiMode.load());
                dc.stop();
            }
            activeDataChannel.store(nullptr);
        }
        else if (mode == DataMode::ACTIVE) {
            // Active mode: Client mở port (myActivePort) → Server gửi file đến port này
            DataChannel dc(myActivePort.load());
            activeDataChannel.store(&dc);
            if (dc.start()) {
                dc.receiveFile(targetFile, totalSize, false, isAsciiMode.load());
                dc.stop();
            }
            activeDataChannel.store(nullptr);
        }
        else {
            cerr << "425 Can't open data connection: no PORT/PASV negotiated" << endl;  // 425: không thể mở kết nối dữ liệu
        }

        // LIST/NLST: đọc file tạm, in từng dòng ra console, rồi xóa file tạm
        if (isList) {
            if (fs::exists(targetFile)) {
                ifstream ifs(targetFile);
                if (ifs) {
                    string line;
                    while (getline(ifs, line)) {
                        cout << "      " << line << "\n";  // Thụt lề 6 ký tự cho đẹp
                    }
                    ifs.close();
                }
                error_code ec;
                fs::remove(targetFile, ec);
            }
        }
    }
    // Reset chế độ data sau mỗi lần truyền
    dataMode = DataMode::NONE;
}

// =====================================================================
// receiverLoop — THREAD PHỤ LẮNG NGHE PHẢN HỒI TỪ SERVER LIÊN TỤC
//   Chạy song song với vòng lặp nhập lệnh trong run().
//   Mỗi phản hồi từ Server là một dòng text kết thúc bằng '\n'.
//   Thread này phân tích mã trạng thái FTP 3 chữ số:
//   - Mã 227: phản hồi PASV → trích xuất port Passive
//   - Mã 150: Server sẵn sàng truyền dữ liệu → kích hoạt doDataTransfer()
//   - Mã kết thúc (isFinalStatusCode): giải phóng thread lệnh đang chờ
//   Phản hồi bất đồng bộ (khi awaitingReply=false): hiển thị bằng WriteConsoleA
//   để không xung đột với prompt "ftp>" hiện tại trên console.
// =====================================================================
void ControlChannel::receiverLoop() {
    char buffer[1024];
    string streamBuffer = "";  // Buffer tích lũy dữ liệu TCP (xử lý trường hợp dữ liệu đến rời rạc)

    while (keepRunning) {
        ZeroMemory(buffer, sizeof(buffer));
        int byteRecv = recv(tcpSocket, buffer, sizeof(buffer) - 1, 0);

        // Kiểm tra ngắt kết nối
        if (byteRecv <= 0) {
            if (keepRunning) {
                if (byteRecv == 0) cout << "\n221 Connection closed by remote host" << endl;  // 221: Server đóng kết nối bình thường
                else cerr << format("\n426 Connection closed, transfer aborted (WSA error: {})", WSAGetLastError()) << endl;  // 426: kết nối bị đóng bất thường
            }
            keepRunning = false;
            {
                lock_guard<mutex> lock(replyMutex);
                awaitingReply = false;
            }
            replyCv.notify_one();  // Giải phóng thread lệnh nếu đang chờ
            break;
        }

        // Thêm dữ liệu nhận được vào buffer tích lũy
        streamBuffer.append(buffer, byteRecv);

        // Xử lý từng dòng phản hồi (phân tách bằng '\n')
        size_t pos = 0;
        while ((pos = streamBuffer.find('\n')) != string::npos) {
            string reply = streamBuffer.substr(0, pos);
            streamBuffer.erase(0, pos + 1);
            // Xóa ký tự '\r' và '\n' ở cuối dòng
            while (!reply.empty() && (reply.back() == '\r' || reply.back() == '\n')) reply.pop_back();

            if (reply.empty()) continue;

            // Phân tích mã trạng thái FTP (3 chữ số đầu dòng)
            string code = (reply.size() >= 3) ? reply.substr(0, 3) : "";
            // Kiểm tra xem dòng có bắt đầu bằng 3 chữ số không
            bool isAnyStatusCode = (reply.size() >= 3 && isdigit((unsigned char)reply[0]) &&
                                 isdigit((unsigned char)reply[1]) && isdigit((unsigned char)reply[2]));
            // Mã trạng thái cuối cùng (final): "NNN " hoặc chỉ "NNN" (không có '-')
            // Mã có '-' sau 3 chữ số = mã trung gian (multi-line reply), vd: "214-..."
            bool isFinalStatusCode = isAnyStatusCode && (reply.size() == 3 || reply[3] == ' ');

            if (isAnyStatusCode) {
                // Hiển thị phản hồi có mã trạng thái
                string displayReply = reply;
                // Thay '-' bằng ' ' để hiển thị đẹp hơn (phản hồi multi-line)
                if (displayReply.size() > 3 && displayReply[3] == '-') displayReply[3] = ' ';
                if (awaitingReply) {
                    // Đang chờ phản hồi → in trực tiếp (thread lệnh đang block)
                    cout << "Server: " << displayReply << endl;
                } else {
                    // Phản hồi bất đồng bộ (vd: 226 Transfer complete sau khi transfer xong)
                    // Dùng WriteConsoleA để ghi trực tiếp, tránh xung đột với prompt "ftp>"
                    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
                    string msg = "\r" + string(70, ' ') + "\rServer: " + displayReply + "\nftp> ";
                    DWORD written;
                    WriteConsoleA(hOut, msg.c_str(), msg.length(), &written, NULL);
                }
            } else {
                // Dòng không có mã trạng thái (phần nội dung của phản hồi multi-line)
                string cleanReply = reply;
                size_t start = cleanReply.find_first_not_of(" \t");  // Xóa khoảng trắng đầu dòng
                if (start != string::npos) cleanReply = cleanReply.substr(start);
                else cleanReply = "";
                if (awaitingReply) {
                    cout << "      " << cleanReply << endl;  // Thụt lề 6 ký tự
                } else {
                    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
                    string msg = "\r" + string(70, ' ') + "\r      " + cleanReply + "\nftp> ";
                    DWORD written;
                    WriteConsoleA(hOut, msg.c_str(), msg.length(), &written, NULL);
                }
            }

            // === Xử lý mã 227: Passive Mode → lưu port Server đã mở ===
            if (code == "227") {
                unsigned short p;
                if (parsePasvReply(reply, p)) {
                    serverPasvPort = p;
                    dataMode = DataMode::PASSIVE;
                }
            }

            // === Xử lý mã 150: Server sẵn sàng → bắt đầu truyền dữ liệu ===
            if (code == "150") {
                // Trích xuất port nhúng trong phản hồi (nếu có "PORT=xxxxx")
                unsigned short p;
                if (parseEmbeddedPort(reply, p)) serverUploadPort = p;

                // Trích xuất kích thước file từ phản hồi (nếu có "(xxx bytes)")
                uintmax_t totalSize = 0;
                size_t startSize = reply.find("(");
                size_t endSize = reply.find(" bytes)");
                if (startSize != string::npos && endSize != string::npos && endSize > startSize) {
                    try { totalSize = stoull(reply.substr(startSize + 1, endSize - startSize - 1)); }
                    catch (...) {}
                }

                // Lấy lệnh và tham số đang chờ (được lưu từ thread nhập lệnh)
                string cmdWord, filename;
                {
                    lock_guard<mutex> lock(pendingMutex);
                    cmdWord = pendingCmdWord;
                    filename = pendingArg;
                }

                // LIST/NLST: truyền đồng bộ (chờ xong mới tiếp tục nhận phản hồi)
                // RETR/STOR...: truyền bất đồng bộ (chạy trên thread riêng, không block receiverLoop)
                if (cmdWord == "LIST" || cmdWord == "NLST") {
                    this->doDataTransfer(cmdWord, filename, totalSize);
                } else {
                    thread([this, cmdWord, filename, totalSize]() {
                        this->doDataTransfer(cmdWord, filename, totalSize);
                    }).detach();
                }

                // Giải phóng thread lệnh đang chờ
                {
                    lock_guard<mutex> lock(replyMutex);
                    awaitingReply = false;
                }
                replyCv.notify_one();
            }
            // === Xử lý mã trạng thái kết thúc (final status code) ===
            else if (isFinalStatusCode) {
                // Mã 426/225: liên quan đến abort transfer → dừng DataChannel đang hoạt động
                if (code == "426" || code == "225") {
                    DataChannel* dc = activeDataChannel.load();
                    if (dc) dc->stop();
                }

                // Giải phóng thread lệnh đang chờ (condition_variable notify)
                {
                    lock_guard<mutex> lock(replyMutex);
                    awaitingReply = false;
                }
                replyCv.notify_one();
            }
        }
    }
}

// =====================================================================
// run — VÒNG LẶP CHÍNH CỦA CLIENT
//   1. Nhận lời chào (greeting) từ Server (thường là "220 Service ready")
//   2. Khởi chạy receiverThread để lắng nghe phản hồi bất đồng bộ
//   3. Vòng lặp: hiện prompt "ftp>" → đọc lệnh → xử lý cục bộ → gửi đến Server
//      → chờ receiverThread đánh thức khi nhận đủ phản hồi (condition_variable)
//   Đồng bộ hóa: thread lệnh đặt awaitingReply=true trước khi gửi, sau đó
//   wait trên replyCv; receiverThread đặt awaitingReply=false và notify khi
//   nhận được mã trạng thái cuối cùng hoặc mã 150 (bắt đầu truyền dữ liệu).
// =====================================================================
void ControlChannel::run() {
    // Nhận lời chào từ Server (thường là "220 Service ready\r\n")
    char buffer[1024] = { 0 };
    int byteRecv = recv(this->tcpSocket, buffer, sizeof(buffer) - 1, 0);

    if (byteRecv > 0) cout << "Server: " << buffer << endl;
    else {
        cerr << "421 Service not available, did not receive greeting from server" << endl;  // 421: dịch vụ không khả dụng
        closesocket(this->tcpSocket);
        this->tcpSocket = INVALID_SOCKET;
        WSACleanup();
        return;
    }

    // Khởi chạy thread phụ lắng nghe phản hồi từ Server
    receiverThread = thread(&ControlChannel::receiverLoop, this);

    string input;
    while (keepRunning) {
        cout << "ftp> " << std::flush;  // Hiện prompt
        if (!getline(cin, input)) break;
        if (input.empty()) continue;

        // Tách lệnh và tham số
        size_t sp = input.find(' ');
        string cmdWord = (sp == string::npos) ? input : input.substr(0, sp);
        for (auto& c : cmdWord) c = toupper(c);  // Chuyển lệnh thành chữ hoa
        string cmdArg = (sp == string::npos) ? "" : input.substr(sp + 1);

        // Xử lý cục bộ lệnh TYPE: cập nhật chế độ ASCII/Binary trên Client
        if (cmdWord == "TYPE") {
            if (cmdArg == "A" || cmdArg == "a") isAsciiMode.store(true);   // ASCII mode
            else if (cmdArg == "I" || cmdArg == "i") isAsciiMode.store(false); // Binary (Image) mode
        }

        // Xử lý cục bộ lệnh PORT: tính port từ tham số, chuyển sang Active mode
        if (cmdWord == "PORT") {
            unsigned short p;
            if (parsePortArgLocal(cmdArg, p)) {
                myActivePort = p;
                dataMode = DataMode::ACTIVE;
            }
        }

        // Kiểm tra: các lệnh truyền dữ liệu yêu cầu phải có PORT hoặc PASV trước
        if ((cmdWord == "RETR" || cmdWord == "LIST" || cmdWord == "NLST" || cmdWord == "STOR" || cmdWord == "STOU" || cmdWord == "APPE") && dataMode.load() == DataMode::NONE) {
            cerr << "425 Can't open data connection: send PORT or PASV before using this command\n";  // 425: không có kết nối dữ liệu
            cout << endl;
            continue;
        }

        // Kiểm tra cục bộ lệnh upload: file phải tồn tại trên Client
        if (cmdWord == "STOR" || cmdWord == "STOU" || cmdWord == "APPE") {
            if (cmdArg.empty()) {
                cerr << "501 Syntax error in parameters\n";  // 501: lỗi cú pháp tham số
                cout << endl;
                continue;
            }
            string localPath = resolvePath(cmdArg).string();
            if (!fs::exists(localPath) || !fs::is_regular_file(localPath)) {
                cerr << "550 File unavailable, local file not found: " << cmdArg << "\n";  // 550: file không tìm thấy
                cout << endl;
                continue;
            }
        }

        // Lưu lệnh đang chờ vào biến chia sẻ (bảo vệ bằng mutex)
        // receiverThread sẽ đọc biến này khi nhận mã 150 để biết cần truyền gì
        {
            lock_guard<mutex> lock(pendingMutex);
            pendingCmdWord = cmdWord;
            pendingArg = cmdArg;
        }

        // Đặt cờ awaitingReply = true trước khi gửi lệnh
        {
            lock_guard<mutex> lock(replyMutex);
            awaitingReply = true;
        }

        // Gửi lệnh FTP đến Server (thêm \r\n theo chuẩn FTP)
        string cmdLine = input + "\r\n";
        send(tcpSocket, cmdLine.c_str(), (int)cmdLine.size(), 0);

        bool isQuit = (cmdWord == "QUIT");

        // Chờ receiverThread đánh thức khi nhận đủ phản hồi
        // condition_variable wait: chỉ thoát khi awaitingReply=false hoặc keepRunning=false
        {
            unique_lock<mutex> lock(replyMutex);
            replyCv.wait(lock, [this] { return !awaitingReply || !keepRunning; });
        }

        if (isQuit) { keepRunning = false; break; }
    }

    stop();
}

// Đóng kết nối TCP và dừng thread nhận phản hồi
void ControlChannel::stop() {
    keepRunning = false;
    if (this->tcpSocket != INVALID_SOCKET) {
        closesocket(this->tcpSocket);
        this->tcpSocket = INVALID_SOCKET;
    }
    // Join thread nhận — kiểm tra không join chính mình (nếu stop() gọi từ receiverThread)
    if (receiverThread.joinable() && receiverThread.get_id() != std::this_thread::get_id()) {
        receiverThread.join();
    }
}
