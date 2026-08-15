// ======================================================================
// HashUtil.cpp — CÀI ĐẶT BĂM FILE SHA-256 BẰNG WINDOWS BCRYPT API
//    Đọc toàn bộ nội dung file vào bộ nhớ, rồi sử dụng BCrypt API
//    (thư viện mã hóa tích hợp sẵn của Windows) để tính SHA-256.
//    BCrypt API không cần cài đặt thêm thư viện bên ngoài (như OpenSSL).
// ======================================================================

// Macro kiểm tra mã trạng thái NTSTATUS (trả về từ các hàm BCrypt)
// NTSTATUS >= 0 → thành công, < 0 → lỗi
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#define WIN32_LEAN_AND_MEAN

#include "HashUtil.h"

// Tính SHA-256 hash của file tại filePath. Trả về chuỗi hex 64 ký tự, hoặc "" nếu lỗi.
// Quy trình: Mở file → Đọc toàn bộ vào RAM → BCryptOpenAlgorithmProvider (SHA-256)
// → BCryptCreateHash → BCryptHashData → BCryptFinishHash → Chuyển kết quả sang hex string
string computeFileSHA256(const string& filePath) {
    // Đọc toàn bộ nội dung file vào vector<char>
    ifstream in(filePath, ios::binary);
    if (!in.is_open()) {
        cerr << "[HASH] Cannot open file: " << filePath << endl;
        return "";
    }
    vector<char> fileData((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
    in.close();

    // --- Bước 1: Mở thuật toán SHA-256 ---
    BCRYPT_ALG_HANDLE hAlg = nullptr;    // Handle thuật toán băm
    BCRYPT_HASH_HANDLE hHash = nullptr;  // Handle phiên băm
    NTSTATUS status = 0;
    DWORD hashObjSize = 0;  // Kích thước object băm nội bộ (BCrypt cần cấp phát)
    DWORD dataSize = 0;     // Kích thước trả về từ BCryptGetProperty
    DWORD hashSize = 0;     // Kích thước kết quả hash (32 byte = 256 bit cho SHA-256)

    // BCryptOpenAlgorithmProvider: mở provider cho thuật toán BCRYPT_SHA256_ALGORITHM
    status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (!NT_SUCCESS(status)) {
        cerr << "[HASH] BCryptOpenAlgorithmProvider failed: " << status << endl;
        return "";
    }

    // --- Bước 2: Truy vấn kích thước object băm và kích thước hash output ---
    // BCRYPT_OBJECT_LENGTH: kích thước bộ nhớ cần cho object băm nội bộ
    status = BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PBYTE)&hashObjSize, sizeof(DWORD), &dataSize, 0);
    if (!NT_SUCCESS(status)) {
        cerr << "[HASH] BCryptGetProperty(OBJECT_LENGTH) failed" << endl;
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return "";
    }

    // BCRYPT_HASH_LENGTH: kích thước kết quả hash (32 byte cho SHA-256)
    status = BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, (PBYTE)&hashSize, sizeof(DWORD), &dataSize, 0);
    if (!NT_SUCCESS(status)) {
        cerr << "[HASH] BCryptGetProperty(HASH_LENGTH) failed" << endl;
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return "";
    }

    // Cấp phát bộ nhớ cho object băm nội bộ
    vector<BYTE> hashObj(hashObjSize);

    // --- Bước 3: Tạo phiên băm ---
    status = BCryptCreateHash(hAlg, &hHash, hashObj.data(), hashObjSize, nullptr, 0, 0);
    if (!NT_SUCCESS(status)) {
        cerr << "[HASH] BCryptCreateHash failed" << endl;
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return "";
    }

    // --- Bước 4: Đưa dữ liệu file vào phiên băm ---
    if (!fileData.empty()) {
        status = BCryptHashData(hHash, (PBYTE)fileData.data(), (ULONG)fileData.size(), 0);
        if (!NT_SUCCESS(status)) {
            cerr << "[HASH] BCryptHashData failed" << endl;
            BCryptDestroyHash(hHash);
            BCryptCloseAlgorithmProvider(hAlg, 0);
            return "";
        }
    }

    // --- Bước 5: Hoàn tất phiên băm và lấy kết quả ---
    vector<BYTE> hashOutput(hashSize);
    status = BCryptFinishHash(hHash, hashOutput.data(), hashSize, 0);
    if (!NT_SUCCESS(status)) {
        cerr << "[HASH] BCryptFinishHash failed" << endl;
        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return "";
    }

    // Giải phóng tài nguyên BCrypt
    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    // --- Bước 6: Chuyển kết quả hash (binary) thành chuỗi hex ---
    ostringstream hexStream;
    hexStream << hex << setfill('0');  // Định dạng hex, padding bằng '0'
    for (DWORD i = 0; i < hashSize; i++) hexStream << setw(2) << (int)hashOutput[i];

    return hexStream.str();  // Chuỗi 64 ký tự hex
}
