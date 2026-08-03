#pragma once
#include "lib.h"

//Cung cấp công cụ tính mã băm computeFileSHA256 (Hash SHA-256) sử dụng Windows BCrypt(CNG) API
//Cho file dữ liệu để kiểm tra tính toàn vẹn (Integrity Check) của file trước và sau khi truyền qua mạng
string computeFileSHA256(const string&);
