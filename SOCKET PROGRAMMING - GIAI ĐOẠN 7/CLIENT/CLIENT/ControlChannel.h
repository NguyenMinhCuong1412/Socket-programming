#pragma once
#include "lib.h"

class DataChannel;
class ControlChannel {
private:
	unsigned short serverTcpPort;
	string serverIp;
	SOCKET tcpSocket;

	atomic<DataMode> dataMode{ DataMode::NONE };
	atomic<unsigned short> myActivePort{ 0 };
	atomic<unsigned short> serverPasvPort{ 0 };
	atomic<unsigned short> serverUploadPort{ 0 };
	atomic<bool> isAsciiMode{ true };
	atomic<DataChannel*> activeDataChannel{ nullptr };

	mutex pendingMutex;
	string pendingCmdWord;
	string pendingArg;
	string currentDir;

	fs::path resolvePath(const string& arg);

	thread receiverThread;
	atomic<bool> keepRunning{ true };

	mutex replyMutex;
	condition_variable replyCv;
	bool awaitingReply = false;

	bool parsePortArgLocal(const string& arg, unsigned short& outPort);
	bool parsePasvReply(const string& reply, unsigned short& outPort);
	bool parseEmbeddedPort(const string& reply, unsigned short& outPort);



	void receiverLoop();
	void doDataTransfer(const string& cmdWord, const string& filename, uintmax_t totalSize = 0);
public:
	ControlChannel(unsigned short, string);
	~ControlChannel();

	bool start();
	void run();
	void stop();
};
