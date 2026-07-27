#include "lib.h"
#include "ControlChannel.h"

int main() {
    //Khởi tạo môi trường socket
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << "421 Service not available, WSAStartup failed" << endl;
        return 1;
    }

    //Khởi tạo TCP
    ControlChannel control(CONTROL_PORT);
    if (!control.start()) {
        WSACleanup();
        return 1;
    }

    control.run(); 

    control.stop();
    WSACleanup();
    return 0;
}