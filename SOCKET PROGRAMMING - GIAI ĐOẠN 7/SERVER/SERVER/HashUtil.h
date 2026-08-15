// ======================================================================
// HashUtil.h — TIỆN ÍCH BĂM FILE SHA-256 CỦA SERVER
//    Khai báo hàm tính mã băm SHA-256 cho file, sử dụng Windows BCrypt API.
//    Được gọi bởi lệnh HASH trong CmdHandler để kiểm tra toàn vẹn file.
// ======================================================================
#pragma once
#include "lib.h"

// Tính mã băm SHA-256 của file tại đường dẫn filePath
// Trả về chuỗi hex 64 ký tự (256 bit / 4 = 64 hex digits), hoặc "" nếu lỗi
string computeFileSHA256(const string&);
