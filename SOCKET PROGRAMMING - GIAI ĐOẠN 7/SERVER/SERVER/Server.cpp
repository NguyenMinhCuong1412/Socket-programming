#include "lib.h"
#include "ControlChannel.h"

int main() {
    //Khởi tạo môi trường socket
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << "421 Service not available, WSAStartup failed" << endl;
        return 1;
    }

    //Khởi tạo thư mục làm việc riêng cho Client
    error_code ec;
    fs::create_directories(SERVER_ROOT, ec);
    if (ec) {
        cerr << format("421 Service not available, cannot create server root '{}': {}", SERVER_ROOT.string(), ec.message()) << endl;
        WSACleanup();
        return 1;
    }
    cout << "Server root: " << SERVER_ROOT.string() << endl;

    //Khởi tạo kênh điều khiển - TCP
    ControlChannel control(CONTROL_PORT);
    if (!control.start()) {
        WSACleanup();
        return 1;
    }
    control.run();
    control.stop();

    //Dọn dẹp môi trường socket
    WSACleanup();
    return 0;
}