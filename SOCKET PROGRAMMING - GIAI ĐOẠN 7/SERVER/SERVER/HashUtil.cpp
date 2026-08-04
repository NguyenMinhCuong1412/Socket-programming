// ============================================================
// HashUtil.cpp — Implement SHA-256 bằng Windows BCrypt API
// BCrypt (CNG) là API hệ điều hành Windows, có sẵn từ Vista trở lên
// Luồng:
//   BCryptOpenAlgorithmProvider (SHA256)
//     → BCryptCreateHash
//       → BCryptHashData (toàn bộ file)
//         → BCryptFinishHash (32 byte output)
//           → Chuyển 32 byte → 64 hex chars
//   BCryptDestroyHash
//   BCryptCloseAlgorithmProvider
// ============================================================


#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0) //Macro kiểm tra lỗi BCrypt (NTSTATUS: >= 0 -> OK)
#define WIN32_LEAN_AND_MEAN                            //Loại bỏ bớt các thư viện/header ít dùng của Windows ra khỏi file header chính <windows.h>, tăng tốc độ biên dịch và tránh xung đột tên hàm/biến

#include "HashUtil.h"

string computeFileSHA256(const string& filePath) {
    //Đọc toàn bộ file vào buffer
    ifstream in(filePath, ios::binary);
    if (!in.is_open()) {
        cerr << "[HASH] Cannot open file: " << filePath << endl;
        return "";
    }
    vector<char> fileData((istreambuf_iterator<char>(in)), istreambuf_iterator<char>()); //Đọc toàn bộ dữ liệu từ tệp vào một mảng động
    in.close();

    //Khởi tạo BCrypt SHA-256
    BCRYPT_ALG_HANDLE hAlg = nullptr;   //Con trỏ quản lý thuật toán cryptography (Algorithm Provider Handle)
    BCRYPT_HASH_HANDLE hHash = nullptr; //Con trỏ quản lý đối tượng hash cụ thể sẽ tạo ra (Hash Handle)
    NTSTATUS status = 0;                //Biến lưu mã trạng thái trả về của các hàm BCrypt
    DWORD hashObjSize = 0;              
    DWORD dataSize = 0;
    DWORD hashSize = 0;

    //Mở Algorithm Provider cho SHA-256
    status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0); //Yêu cầu Windows cấp quyền sử dụng thuật toán SHA-256. Lấy con trỏ kết quả gán vào hAlg
    if (!NT_SUCCESS(status)) {
        cerr << "[HASH] BCryptOpenAlgorithmProvider failed: " << status << endl;
        return "";
    }

    //Lấy kích thước hash object (cần để cấp phát buffer nội bộ)
    status = BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PBYTE)&hashObjSize, sizeof(DWORD), &dataSize, 0); //Hỏi Windows xem thuật toán này cần bao nhiêu byte bộ nhớ cho state/object nội bộ (hashObjSize)
    if (!NT_SUCCESS(status)) {
        cerr << "[HASH] BCryptGetProperty(OBJECT_LENGTH) failed" << endl;
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return "";
    }

    //Lấy kích thước hash output (SHA-256 = 32 byte)
    status = BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, (PBYTE)&hashSize, sizeof(DWORD), &dataSize, 0); //Hỏi kích thước kết quả của băm (hashSize)
    if (!NT_SUCCESS(status)) {
        cerr << "[HASH] BCryptGetProperty(HASH_LENGTH) failed" << endl;
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return "";
    }
   
    vector<BYTE> hashObj(hashObjSize); //Cấp phát buffer cho hash object

    //Tạo hash object
    status = BCryptCreateHash(hAlg, &hHash, hashObj.data(), hashObjSize, nullptr, 0, 0); //Khởi tạo phiên tính toán hash mới, ghi quản lý phiên vào hHash
    if (!NT_SUCCESS(status)) {
        cerr << "[HASH] BCryptCreateHash failed" << endl;
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return "";
    }

    //Nạp dữ liệu file vào hash
    if (!fileData.empty()) {
        status = BCryptHashData(hHash, (PBYTE)fileData.data(), (ULONG)fileData.size(), 0); //Nạp toàn bộ mảng byte của file (fileData) vào đối tượng hash để tiến hành tính toán
        if (!NT_SUCCESS(status)) {
            cerr << "[HASH] BCryptHashData failed" << endl;
            BCryptDestroyHash(hHash);
            BCryptCloseAlgorithmProvider(hAlg, 0);
            return "";
        }
    }

    //Lấy kết quả hash (32 byte)
    vector<BYTE> hashOutput(hashSize); //Cấp phát mảng chứa kết quả băm (32 byte nhị phân)
    status = BCryptFinishHash(hHash, hashOutput.data(), hashSize, 0); //Hoàn tất quá trình băm và chép kết quả 32-byte nhị phân vào hashOutput
    if (!NT_SUCCESS(status)) {
        cerr << "[HASH] BCryptFinishHash failed" << endl;
        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return "";
    }

    //Dọn dẹp
    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    //Chuyển 32 byte → 64 hex chars (lowercase)
    ostringstream hexStream;
    hexStream << hex << setfill('0');
    for (DWORD i = 0; i < hashSize; i++) hexStream << setw(2) << (int)hashOutput[i];

    return hexStream.str();
}
