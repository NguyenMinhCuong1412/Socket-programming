#include "lib.h"
#include "ControlChannel.h"

int main(int argc, char* argv[]) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << "421 Service not available, WSAStartup failed" << endl;
        return 1;
    }

    error_code ec;
    fs::create_directories(CLIENT_ROOT, ec);
    if (ec) {
        cerr << format("421 Service not available, cannot create client root '{}': {}", CLIENT_ROOT.string(), ec.message()) << endl;
        WSACleanup();
        return 1;
    }
    cout << "Client root: " << CLIENT_ROOT.string() << endl;

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
