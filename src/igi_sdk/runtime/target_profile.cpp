#include "igi_sdk/runtime/target_profile.hpp"

#include "igi_sdk/runtime/igi1_hooks.hpp"

#include <array>
#include <cstring>

#if defined(_WIN32)
#include <windows.h>
#include <wincrypt.h>
#endif

namespace igi::igi1 {
bool isSupportedExecutable() noexcept {
#if defined(_WIN32)
    std::array<char, MAX_PATH> executablePath{};
    if (GetModuleFileNameA(nullptr, executablePath.data(), static_cast<DWORD>(executablePath.size())) == 0) return false;
    HANDLE file = CreateFileA(executablePath.data(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    bool valid = CryptAcquireContextA(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) != FALSE &&
                 CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash) != FALSE;
    std::array<std::byte, 4096> buffer{};
    DWORD read = 0;
    while (valid) {
        if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)) {
            valid = false;
            break;
        }
        if (read == 0) break;
        valid = CryptHashData(hash, reinterpret_cast<const BYTE*>(buffer.data()), read, 0) != FALSE;
    }
    std::array<BYTE, 32> digest{};
    DWORD digestSize = static_cast<DWORD>(digest.size());
    valid = valid && CryptGetHashParam(hash, HP_HASHVAL, digest.data(), &digestSize, 0) != FALSE && digestSize == digest.size();
    if (hash) CryptDestroyHash(hash);
    if (provider) CryptReleaseContext(provider, 0);
    CloseHandle(file);

    constexpr std::array<BYTE, 32> expected = {
        0xde, 0xa3, 0x48, 0x92, 0xd4, 0x17, 0x71, 0x58,
        0x73, 0xea, 0x61, 0xd7, 0xb2, 0xba, 0x55, 0x56,
        0x65, 0x5f, 0x1a, 0xd6, 0x53, 0x85, 0x10, 0x43,
        0x48, 0x14, 0x6f, 0x96, 0x79, 0x91, 0x5a, 0x01};
    return valid && std::memcmp(digest.data(), expected.data(), digest.size()) == 0;
#else
    return false;
#endif
}
}
