#pragma once
#include "lib.h"

struct ClientRecord {
    string ip;
    string userName;
    bool   loggedIn;
    string currentDir;
    string lastCommand;
    chr::system_clock::time_point connectedAt;
};

class SessionRegistry {
public:
    static void add(SOCKET sock, const string& ip);
    static void remove(SOCKET sock);
    static void update(SOCKET sock, const string& userName, bool loggedIn,
        const string& currentDir, const string& lastCommand);

    static void printTable();
    static size_t count();

private:
    static mutex mtx;
    static map<SOCKET, ClientRecord> table;
};
