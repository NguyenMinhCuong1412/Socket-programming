#include "lib.h"
#include "ControlChannel.h"

int main() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << "421 Service not available, WSAStartup failed" << endl;
        return 1;
    }

    //Tạo thư mục gốc riêng của server (KHÔNG dùng thẳng thư mục project - xem lib.h)
    std::error_code ec;
    fs::create_directories(SERVER_ROOT, ec);
    if (ec) {
        cerr << format("421 Service not available, cannot create server root '{}': {}", SERVER_ROOT.string(), ec.message()) << endl;
        WSACleanup();
        return 1;
    }
    cout << "Server root: " << SERVER_ROOT.string() << endl;

    ControlChannel control(CONTROL_PORT);

    if (!control.start()) {
        WSACleanup();
        return 1;
    }

    //run() giờ chạy vô hạn, accept nhiều client cùng lúc (mỗi client 1 thread)
    control.run();

    control.stop();

    WSACleanup();
    return 0;
}