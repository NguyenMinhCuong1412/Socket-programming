// ======================================================================
// SessionRegistry.cpp — CÀI ĐẶT BẢNG ĐĂNG KÝ PHIÊN LÀM VIỆC
//    Triển khai các thao tác CRUD trên bảng phiên: thêm, xóa, cập nhật,
//    đếm, và in bảng trạng thái. Tất cả đều thread-safe (lock_guard<mutex>).
// ======================================================================
#include "SessionRegistry.h"

// Khởi tạo static members
mutex SessionRegistry::mtx;
map<SOCKET, ClientRecord> SessionRegistry::table;

// Thêm Client mới vào bảng — gọi khi accept() kết nối TCP thành công
void SessionRegistry::add(SOCKET sock, const string& ip) {
    lock_guard<mutex> lock(mtx);  // Khóa mutex để đảm bảo thread-safe
    ClientRecord rec;
    rec.ip = ip;
    rec.userName = "";
    rec.loggedIn = false;
    rec.currentDir = "/";
    rec.lastCommand = "";
    rec.connectedAt = chr::system_clock::now();
    table[sock] = rec;
}

// Xóa Client khỏi bảng — gọi khi Client ngắt kết nối
void SessionRegistry::remove(SOCKET sock) {
    lock_guard<mutex> lock(mtx);
    table.erase(sock);
}

// Cập nhật thông tin phiên — gọi sau mỗi lần xử lý lệnh FTP
void SessionRegistry::update(SOCKET sock, const string& userName, bool loggedIn,
    const string& currentDir, const string& lastCommand) {
    lock_guard<mutex> lock(mtx);
    auto it = table.find(sock);
    if (it == table.end()) return;  // Socket không tồn tại trong bảng
    it->second.userName = userName;
    it->second.loggedIn = loggedIn;
    it->second.currentDir = currentDir;
    it->second.lastCommand = lastCommand;
}

// Đếm số phiên đang hoạt động
size_t SessionRegistry::count() {
    lock_guard<mutex> lock(mtx);
    return table.size();
}

// In bảng trạng thái tất cả phiên ra console Server
// Khóa cả mutex bảng phiên (mtx) VÀ mutex console (g_coutMutex) để tránh output xen lẫn
void SessionRegistry::printTable() {
    lock_guard<mutex> lockTable(mtx);       // Khóa bảng phiên
    lock_guard<mutex> lockCout(g_coutMutex); // Khóa console output

    cout << "==================== ACTIVE SESSION TABLE ====================\n";
    if (table.empty()) {
        cout << "(no active sessions)\n";
    }
    else {
        // In header bảng với định dạng cột
        cout << format("{:<16}{:<12}{:<10}{:<14}{:<10}\n", "IP", "User", "LoggedIn", "CurrentDir", "LastCmd");
        // In từng hàng — structured binding (C++17): [sock, rec] = cặp key-value trong map
        for (auto& [sock, rec] : table) {
            cout << format("{:<16}{:<12}{:<10}{:<14}{:<10}\n",
                rec.ip,
                rec.userName.empty() ? "(none)" : rec.userName,
                rec.loggedIn ? "yes" : "no",
                rec.currentDir,
                rec.lastCommand.empty() ? "-" : rec.lastCommand);
        }
    }
    cout << format("Total active sessions: {}\n", table.size());
    cout << "================================================================" << endl;
}
