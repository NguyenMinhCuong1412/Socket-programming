#pragma once
#include <string>

// ============================================================
// HashUtil.h — Tính SHA-256 hash cho file
// Giai đoạn 7: Data Integrity verification
//
// Sử dụng BCrypt API (Windows CNG — Cryptography: Next Generation)
// Đây là API hệ điều hành, KHÔNG phải thư viện bên thứ 3
// ============================================================

// Tính SHA-256 hash của file tại đường dẫn cho trước
// Return: chuỗi hex 64 ký tự (lowercase), rỗng nếu lỗi
std::string computeFileSHA256(const std::string& filePath);
