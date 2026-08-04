#include "lib.h"
#include "ControlChannel.h"

int main(int argc, char* argv[]) {
    //Xác định IP của Server
    string serverIp = "127.0.0.1";
    if (argc > 1) {
        serverIp = argv[1];
    } else {
        cout << "Nhap IP Server (de trong dung mac dinh 127.0.0.1): ";
        string inputIp;
        if (getline(cin, inputIp) && !inputIp.empty()) {
            serverIp = inputIp;
        }
    }
    cout << "Dang ket noi toi Server IP: " << serverIp << ":" << CONTROL_PORT << "..." << endl;

    //Khởi tạo môi trường socket
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << "421 Service not available, WSAStartup failed" << endl;
        return 1;
    }

    //Khởi tạo kênh điều khiển - TCP
    ControlChannel control(CONTROL_PORT, serverIp);
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
