#include "lib.h"
#include "ControlChannel.h"

int main() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << "421 Service not available, WSAStartup failed" << endl;
        return 1;
    }

    ControlChannel control(CONTROL_PORT, "127.0.0.1");
    if (!control.start()) {
        WSACleanup();
        return 1;
    }

    control.run();
    control.stop();
    WSACleanup();
    return 0;
}