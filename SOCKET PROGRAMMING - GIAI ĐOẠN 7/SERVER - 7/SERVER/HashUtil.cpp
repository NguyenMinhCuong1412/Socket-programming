// ============================================================
// HashUtil.cpp — Implement SHA-256 bằng Windows BCrypt API
//
// BCrypt (CNG) là API hệ điều hành Windows, có sẵn từ Vista trở lên.
// Không cần cài thêm thư viện nào.
//
// Luồng:
//   BCryptOpenAlgorithmProvider (SHA256)
//     → BCryptCreateHash
//       → BCryptHashData (toàn bộ file)
//         → BCryptFinishHash (32 byte output)
//           → Chuyển 32 byte → 64 hex chars
//   BCryptDestroyHash
//   BCryptCloseAlgorithmProvider
// ============================================================

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")

#include "HashUtil.h"
#include <fstream>
#include <vector>
#include <sstream>
#include <iomanip>
#include <iostream>

// Macro kiểm tra lỗi BCrypt (NTSTATUS: >= 0 là OK)
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)

std::string computeFileSHA256(const std::string& filePath) {
    // ----- Đọc toàn bộ file vào buffer -----
    std::ifstream in(filePath, std::ios::binary);
    if (!in.is_open()) {
        std::cerr << "[HASH] Cannot open file: " << filePath << std::endl;
        return "";
    }
    std::vector<char> fileData((std::istreambuf_iterator<char>(in)),
        std::istreambuf_iterator<char>());
    in.close();

    // ----- Khởi tạo BCrypt SHA-256 -----
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    NTSTATUS status = 0;
    DWORD hashObjSize = 0;
    DWORD dataSize = 0;
    DWORD hashSize = 0;

    // Mở algorithm provider cho SHA-256
    status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (!NT_SUCCESS(status)) {
        std::cerr << "[HASH] BCryptOpenAlgorithmProvider failed: " << status << std::endl;
        return "";
    }

    // Lấy kích thước hash object (cần để cấp phát buffer nội bộ)
    status = BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH,
        (PBYTE)&hashObjSize, sizeof(DWORD), &dataSize, 0);
    if (!NT_SUCCESS(status)) {
        std::cerr << "[HASH] BCryptGetProperty(OBJECT_LENGTH) failed" << std::endl;
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return "";
    }

    // Lấy kích thước hash output (SHA-256 = 32 byte)
    status = BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH,
        (PBYTE)&hashSize, sizeof(DWORD), &dataSize, 0);
    if (!NT_SUCCESS(status)) {
        std::cerr << "[HASH] BCryptGetProperty(HASH_LENGTH) failed" << std::endl;
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return "";
    }

    // Cấp phát buffer cho hash object
    std::vector<BYTE> hashObj(hashObjSize);

    // Tạo hash object
    status = BCryptCreateHash(hAlg, &hHash, hashObj.data(), hashObjSize, nullptr, 0, 0);
    if (!NT_SUCCESS(status)) {
        std::cerr << "[HASH] BCryptCreateHash failed" << std::endl;
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return "";
    }

    // ----- Nạp dữ liệu file vào hash -----
    if (!fileData.empty()) {
        status = BCryptHashData(hHash, (PBYTE)fileData.data(), (ULONG)fileData.size(), 0);
        if (!NT_SUCCESS(status)) {
            std::cerr << "[HASH] BCryptHashData failed" << std::endl;
            BCryptDestroyHash(hHash);
            BCryptCloseAlgorithmProvider(hAlg, 0);
            return "";
        }
    }

    // ----- Lấy kết quả hash (32 byte) -----
    std::vector<BYTE> hashOutput(hashSize);
    status = BCryptFinishHash(hHash, hashOutput.data(), hashSize, 0);
    if (!NT_SUCCESS(status)) {
        std::cerr << "[HASH] BCryptFinishHash failed" << std::endl;
        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return "";
    }

    // ----- Dọn dẹp -----
    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    // ----- Chuyển 32 byte → 64 hex chars (lowercase) -----
    std::ostringstream hexStream;
    hexStream << std::hex << std::setfill('0');
    for (DWORD i = 0; i < hashSize; i++) {
        hexStream << std::setw(2) << (int)hashOutput[i];
    }

    return hexStream.str();
}
