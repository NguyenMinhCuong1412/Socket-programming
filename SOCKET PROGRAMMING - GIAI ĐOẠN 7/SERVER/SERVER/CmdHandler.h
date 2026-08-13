#pragma once
#include "lib.h"
#include "Session.h"

void parseCmd(const string&, string&, string&);

FtpCommand toFtpCommand(const string& command);

class CommandHandler {
private:
    SOCKET clientSocket;
    string clientIp;
    thread transferThread;

    void joinPreviousTransfer();
    void sendIntermediate(const string& msg);
    fs::path resolvePath(const Session& s, const string& arg, string& outLogical);

    string handleUser(Session&, const string&);
    string handlePass(Session&, const string&);
    string handlePwd(Session&);
    string handleNoop();
    string handleQuit();
    string handleHelp(const string&);
    string handleType(Session&, const string&);
    string handleMode(Session&, const string&);
    string handleSize(Session&, const string&);
    string handleStat(Session&, const string&);
    string handleMdtm(Session&, const string&);
    string handleStor(Session&, const string&);
    string handleRetr(Session&, const string&);
    string handleCwd(Session&, const string&);
    string handleCdup(Session&);
    string handleMkd(Session&, const string&);
    string handleRmd(Session&, const string&);
    string handleList(Session&, const string&);
    string handleNlst(Session&, const string&);
    string handleStou(Session&, const string&);
    string handleAppe(Session&, const string&);
    string handleDele(Session&, const string&);
    string handleRnfr(Session&, const string&);
    string handleRnto(Session&, const string&);
    string handleHash(Session&, const string&);
    string handlePort(Session&, const string&);
    string handlePasv(Session&);
    string handleAbor(Session&);

    unsigned short pickListenPort(Session&);
    string appendPortIfNeeded(Session&, unsigned short, const string&);
public:
    CommandHandler();
    ~CommandHandler();

    void setControlSocket(SOCKET s) { this->clientSocket = s; }
    void setClientIp(const string& ip) { this->clientIp = ip; }

    string handle(Session&, const string&, const string&);
};