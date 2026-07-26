#include "CommandHandler.h"

FtpCmd toFtpCmd(const string& cmd) {
	if (cmd == "USER") return FtpCmd::USER;
	if (cmd == "PASS") return FtpCmd::PASS;
	if (cmd == "QUIT") return FtpCmd::QUIT;
	if (cmd == "NOOP") return FtpCmd::NOOP;
	if (cmd == "PWD") return FtpCmd::PWD;
	if (cmd == "CWD") return FtpCmd::CWD;
	if (cmd == "CDUP") return FtpCmd::CDUP;
	if (cmd == "MKD") return FtpCmd::MKD;
	if (cmd == "RMD") return FtpCmd::RMD;
	if (cmd == "LIST") return FtpCmd::LIST;
	if (cmd == "NLST") return FtpCmd::NLST;
	if (cmd == "STAT") return FtpCmd::STAT;
	if (cmd == "SIZE") return FtpCmd::SIZE;
	if (cmd == "MDTM") return FtpCmd::MDTM;
	if (cmd == "TYPE") return FtpCmd::TYPE;
	if (cmd == "MODE") return FtpCmd::MODE;
	if (cmd == "PORT") return FtpCmd::PORT;
	if (cmd == "PASV") return FtpCmd::PASV;
	if (cmd == "RETR") return FtpCmd::RETR;
	if (cmd == "STOR") return FtpCmd::STOR;
	if (cmd == "STOU") return FtpCmd::STOU;
	if (cmd == "APPE") return FtpCmd::APPE;
	if (cmd == "DELE") return FtpCmd::DELE;
	if (cmd == "RNFR") return FtpCmd::RNFR;
	if (cmd == "RNTO") return FtpCmd::RNTO;
	if (cmd == "HASH") return FtpCmd::HASH;
	if (cmd == "ABOR") return FtpCmd::ABOR;
	if (cmd == "HELP") return FtpCmd::HELP;
	return FtpCmd::UNKNOWN;
}

//Giai đoạn 2
string CommandHandler::handleUser(Session& s, const string& arg) {
	if (arg.empty()) return "501 Syntax error in parameters\r\n";
	s.setName(arg);
	return "331 Username OK, need password\r\n";
}

string CommandHandler::handlePass(Session& s, const string& arg) {
	if (s.getName().empty()) return "503 Bad sequence of commands\r\n";
	if (arg.empty()) return "501 Syntax error in parameters\r\n";
	s.setLogged(true);
	return "230 Login successful\r\n";
}

string CommandHandler::handlePwd(Session& s) {
	if (!s.getLogged()) return "530 Not logged in\r\n";
	return format("257 \"{}\" is current directory\r\n", s.getDir());
}

string CommandHandler::handleNoop() { return "200 NOOP OK\r\n"; }

string CommandHandler::handleQuit() { return "221 Goodbye\r\n"; }

string CommandHandler::handleHelp(const string& arg) {
	if (arg.empty()) {
		string response = "214 The following commands are recognized:\r\n";
		response += "        USER  PASS  QUIT  NOOP  PWD  CWD  CDUP  MKD\n";
		response += "        RMD  LIST  NLST  STAT  SIZE  MDTM  TYPE  MODE\n";
		response += "        PORT  PASV  RETR  STOR  STOU  APPE  DELE  RNFR\n";
		response += "        RNTO  HASH  ABOR  HELP\n";
		response += "        214 Direct queries to this command using 'HELP <command>'\r\n";
		return response;
	}

	string cmd = arg;
	for (auto& c : cmd) c = toupper(c);

	switch (toFtpCmd(cmd)) {
	case FtpCmd::USER: return "214 Syntax: USER <username> - Send username to start authentication\r\n";
	case FtpCmd::PASS: return "214 Syntax: PASS <password> - Send password to complete authentication\r\n";
	case FtpCmd::PWD:  return "214 Syntax: PWD - Print current working directory\r\n";
	case FtpCmd::NOOP: return "214 Syntax: NOOP - No operation, keep-alive ping\r\n";
	case FtpCmd::QUIT: return "214 Syntax: QUIT - Terminate the control connection\r\n";
	case FtpCmd::HELP: return "214 Syntax: HELP [command] - Help text for all supported commands, or detailed usage for a specific command\r\n";
	case FtpCmd::TYPE: return "214 Syntax: TYPE {A|I} - Set the data transfer type\r\n";
	case FtpCmd::MODE: return "214 Syntax: MODE {S|B|C} - Set the transfer mode\r\n";
	case FtpCmd::SIZE: return "214 Syntax: SIZE <filename> - Return the exact byte size of the specified file on the server\r\n";
	case FtpCmd::MDTM: return "214 Syntax: MDTM <filename> - Return the last modification timestamp of the specified file\r\n";
	case FtpCmd::STAT: return "214 Syntax: STAT [path] - Return server status or, if a path is given, file/directory metadata\r\n";
	case FtpCmd::STOR: return "214 Syntax: STOR <filename> - Store (upload) a file from the client to the server using the current filename\r\n";
	case FtpCmd::RETR: return "214 Syntax: RETR <filename> - Retrieve (download) the specified file from the server to the client\r\n";
	default: return format("501 Syntax error in parameters, unknown command '{}'\r\n", cmd);
	}
}

//Giai đoạn 3
string CommandHandler::handleType(Session& s, const string& arg) {
	if (!s.getLogged()) return "530 Not logged in\r\n";
	string t = arg;
	for (auto& c : t) c = toupper(c);
	if (t != "A" && t != "I") return "501 Syntax error in parameters\r\n";
	s.setType(t);
	return format("200 Type set to {}\r\n", t);
}

string CommandHandler::handleMode(Session& s, const string& arg) {
	if (!s.getLogged()) return "530 Not logged in\r\n";
	string m = arg;
	for (auto& c : m) c = toupper(c);
	if (m != "S" && m != "B" && m != "C")return "501 Syntax error in parameters\r\n";
	s.setMode(m);
	return format("200 Mode set to {}\r\n", m);
}

string CommandHandler::handleSize(Session& s, const string& arg) {
	if (!s.getLogged()) return "530 Not logged in\r\n";
	if (arg.empty()) return "501 Syntax error in parameters\r\n";

	fs::path filePath = fs::absolute(fs::path(s.getDir())/arg);        //Lấy đường dẫn chính xác
	if (!fs::exists(filePath) || !fs::is_regular_file(filePath))       //Kiểm tra tồn tại và phải là định dạng file
		return format("550 File unavailable, '{}' not found\r\n", arg);
	return format("213 {}\r\n", fs::file_size(filePath));              //Trả về kích thước file - byte
}

string CommandHandler::handleMdtm(Session& s, const string& arg) {
	if (!s.getLogged()) return "530 Not logged in\r\n";
	if (arg.empty()) return "501 Syntax error in parameters\r\n";

	fs::path filePath = fs::absolute(fs::path(s.getDir()) / arg);
	if (!fs::exists(filePath) || !fs::is_regular_file(filePath))
		return format("550 File unavailable, '{}' not found\r\n", arg);

	auto ftime = fs::last_write_time(filePath);             //Kiểu dữ liệu std::filesystem::file_time_type
	auto sctp = chr::clock_cast<chr::system_clock>(ftime);  //Kiểu dữ liệu std::chrono::system_clock::time_point
	auto tt = chr::system_clock::to_time_t(sctp);           //Kiểu dữ liệu std::chrono::system_clock::time_t
	std::tm tm;                                             //struct tm chứa các thông tin thời gian
	localtime_s(&tm, &tt);                                  //Sao chép các thông tin thời gian của 'tt' vào biến 'tm'

	return format("213 {:04}{:02}{:02}{:02}{:02}{:02}\r\n", //Xuất theo định dạng "YYYYMMDDhhmmss"
		tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, 
		tm.tm_hour, tm.tm_min, tm.tm_sec);
}

string CommandHandler::handleStat(Session& s, const string& arg) {
	if (!s.getLogged()) return "530 Not logged in\r\n";
	if (arg.empty()) {
		string response = "211 Server status:\r\n";
		response += format(" Connected to user: {}\r\n", s.getName());
		response += format(" Current directory: {}\r\n", s.getDir());
		response += format(" Transfer type: {}, Mode: {}\r\n", s.getType(), s.getMode());
		response += "211 End of status\r\n";
		return response;
	}
	else {
		fs::path filePath = fs::absolute(fs::path(s.getDir()) / arg);
		if (!fs::exists(filePath)) 
			return format("550 File unavailable, '{}' not found\r\n", arg);
		uintmax_t size = std::filesystem::is_regular_file(filePath) ? std::filesystem::file_size(filePath) : 0;
		string res = format("211 Status of {}:\r\n Size: {} bytes\r\n211 End of status\r\n", arg, size);
		return res;
	}
}




string CommandHandler::handle(Session& s, const string& cmd, const string& arg) {
	switch (toFtpCmd(cmd)) {
	case FtpCmd::USER:return handleUser(s, arg);
	case FtpCmd::PASS:return handlePass(s, arg);
	case FtpCmd::PWD:return handlePwd(s);
	case FtpCmd::NOOP:return handleNoop(); 
	case FtpCmd::QUIT:return handleQuit();
	case FtpCmd::HELP:return handleHelp(arg);
	case FtpCmd::TYPE:return handleType(s, arg);
	case FtpCmd::MODE:return handleMode(s, arg);
	case FtpCmd::SIZE: return handleSize(s, arg);
	case FtpCmd::MDTM: return handleMdtm(s, arg);
	case FtpCmd::STAT: return handleStat(s, arg);
	case FtpCmd::STOR: return handleStor(s, arg);
	case FtpCmd::RETR: return handleRetr(s, arg);
	default: return "502 Command not implemented\r\n";
	}
}