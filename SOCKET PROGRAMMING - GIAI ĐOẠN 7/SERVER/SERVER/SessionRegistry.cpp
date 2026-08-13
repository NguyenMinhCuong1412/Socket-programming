#include "SessionRegistry.h"

mutex SessionRegistry::mtx;
map<SOCKET, ClientRecord> SessionRegistry::table;

void SessionRegistry::add(SOCKET sock, const string& ip) {
    lock_guard<mutex> lock(mtx);
    ClientRecord rec;
    rec.ip = ip;
    rec.userName = "";
    rec.loggedIn = false;
    rec.currentDir = "/";
    rec.lastCommand = "";
    rec.connectedAt = chr::system_clock::now();
    table[sock] = rec;
}

void SessionRegistry::remove(SOCKET sock) {
    lock_guard<mutex> lock(mtx);
    table.erase(sock);
}

void SessionRegistry::update(SOCKET sock, const string& userName, bool loggedIn,
    const string& currentDir, const string& lastCommand) {
    lock_guard<mutex> lock(mtx);
    auto it = table.find(sock);
    if (it == table.end()) return;
    it->second.userName = userName;
    it->second.loggedIn = loggedIn;
    it->second.currentDir = currentDir;
    it->second.lastCommand = lastCommand;
}

size_t SessionRegistry::count() {
    lock_guard<mutex> lock(mtx);
    return table.size();
}

void SessionRegistry::printTable() {
    lock_guard<mutex> lockTable(mtx);
    lock_guard<mutex> lockCout(g_coutMutex);

    cout << "==================== ACTIVE SESSION TABLE ====================\n";
    if (table.empty()) {
        cout << "(no active sessions)\n";
    }
    else {
        cout << format("{:<16}{:<12}{:<10}{:<14}{:<10}\n", "IP", "User", "LoggedIn", "CurrentDir", "LastCmd");
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
