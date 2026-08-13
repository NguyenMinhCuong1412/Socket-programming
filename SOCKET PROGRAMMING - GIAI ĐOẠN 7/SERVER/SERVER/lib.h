#pragma once

#include <iostream>
#include <format>
#include <vector>
#include <string>
#include <map>
#include <sstream>
#include <fstream>
#include <cctype>
#include <ctime>
#include <system_error>
#include <filesystem>
#include <chrono>
#include <mutex>
#include <thread>
#include <atomic>
#include <memory>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>
#include <cstdint>
#include <random>
#include <iomanip>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "ws2_32.lib")

using std::cerr, std::cout, std::cin,
std::stoi, std::endl,
std::format, std::toupper,
std::string, std::ios,
std::vector, std::atomic,
std::stringstream,
std::ostringstream,
std::istringstream,
std::ifstream,
std::ofstream,
std::tm,
std::error_code,
std::mutex,
std::lock_guard,
std::map,
std::thread,
std::shared_ptr,
std::make_shared,
std::istreambuf_iterator,
std::hex,
std::setfill,
std::setw,
std::mt19937,
std::random_device,
std::uniform_int_distribution;

namespace fs = std::filesystem;
namespace chr = std::chrono;

constexpr unsigned short CONTROL_PORT = 8080;
constexpr int CHUNK_SIZE = 1024;

inline const fs::path SERVER_ROOT = fs::current_path() / "server_root";
inline mutex g_coutMutex;

enum class DataMode {
	ACTIVE,
	PASSIVE
};

enum class FtpCommand {
    USER, PASS, QUIT, NOOP, PWD,
    CWD, CDUP, MKD, RMD, LIST,
    NLST, STAT, SIZE, MDTM, TYPE,
    MODE, PORT, PASV, RETR, STOR,
    STOU, APPE, DELE, RNFR, RNTO,
    HASH, ABOR, HELP,
    UNKNOWN
};
