#include "lib.h"
#include "ControlChannel.h"

int main() {
    //Khởi tạo môi trường socket
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << "421 Service not available , WSAStartup failed" << endl;
        return 1;
    }

    //Khởi tạo kênh điều khiển - TCP
    ControlChannel control(CONTROL_PORT, "127.0.0.1");

    //Khởi tạo Client-TCP
    if (!control.start()) {
        WSACleanup();
        return 1;
    }

    //Chạy Client-TCP - gửi lệnh/nhận phản hồi
    control.run();

    //Dừng Client-TCP
    control.stop();

    //Dọn dẹp môi trường socket
    WSACleanup();
    return 0;
}