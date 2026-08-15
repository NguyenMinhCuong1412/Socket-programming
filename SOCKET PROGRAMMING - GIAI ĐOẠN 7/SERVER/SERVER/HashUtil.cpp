

#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#define WIN32_LEAN_AND_MEAN

#include "HashUtil.h"

string computeFileSHA256(const string& filePath) {
    ifstream in(filePath, ios::binary);
    if (!in.is_open()) {
        cerr << "[HASH] Cannot open file: " << filePath << endl;
        return "";
    }
    vector<char> fileData((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
    in.close();

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    NTSTATUS status = 0;
    DWORD hashObjSize = 0;
    DWORD dataSize = 0;
    DWORD hashSize = 0;

    status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (!NT_SUCCESS(status)) {
        cerr << "[HASH] BCryptOpenAlgorithmProvider failed: " << status << endl;
        return "";
    }

    status = BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PBYTE)&hashObjSize, sizeof(DWORD), &dataSize, 0);
    if (!NT_SUCCESS(status)) {
        cerr << "[HASH] BCryptGetProperty(OBJECT_LENGTH) failed" << endl;
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return "";
    }

    status = BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, (PBYTE)&hashSize, sizeof(DWORD), &dataSize, 0);
    if (!NT_SUCCESS(status)) {
        cerr << "[HASH] BCryptGetProperty(HASH_LENGTH) failed" << endl;
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return "";
    }

    vector<BYTE> hashObj(hashObjSize);

    status = BCryptCreateHash(hAlg, &hHash, hashObj.data(), hashObjSize, nullptr, 0, 0);
    if (!NT_SUCCESS(status)) {
        cerr << "[HASH] BCryptCreateHash failed" << endl;
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return "";
    }

    if (!fileData.empty()) {
        status = BCryptHashData(hHash, (PBYTE)fileData.data(), (ULONG)fileData.size(), 0);
        if (!NT_SUCCESS(status)) {
            cerr << "[HASH] BCryptHashData failed" << endl;
            BCryptDestroyHash(hHash);
            BCryptCloseAlgorithmProvider(hAlg, 0);
            return "";
        }
    }

    vector<BYTE> hashOutput(hashSize);
    status = BCryptFinishHash(hHash, hashOutput.data(), hashSize, 0);
    if (!NT_SUCCESS(status)) {
        cerr << "[HASH] BCryptFinishHash failed" << endl;
        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return "";
    }

    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    ostringstream hexStream;
    hexStream << hex << setfill('0');
    for (DWORD i = 0; i < hashSize; i++) hexStream << setw(2) << (int)hashOutput[i];

    return hexStream.str();
}
