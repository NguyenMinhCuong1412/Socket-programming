#pragma once

#include <iostream>
#include <format>
#include <vector>
#include <string>
#include <sstream>
#include <fstream>
#include <cctype>
#include <ctime>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <memory>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <cstdint>
#include <random>
#include <iomanip>
#include <filesystem>
#include <system_error>

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
std::mutex,
std::lock_guard,
std::unique_lock,
std::condition_variable,
std::thread,
std::shared_ptr,
std::make_shared,
std::istreambuf_iterator,
std::hex,
std::setfill,
std::setw,
std::mt19937,
std::random_device,
std::uniform_int_distribution,
std::error_code;

namespace chr = std::chrono;
namespace fs = std::filesystem;

constexpr unsigned short CONTROL_PORT = 8080;
constexpr int CHUNK_SIZE = 1024;

inline const fs::path CLIENT_ROOT = fs::current_path() / "client_root";

enum class DataMode {
	NONE,
	ACTIVE,
	PASSIVE
};
