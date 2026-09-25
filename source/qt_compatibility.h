#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <array>
#include <cstring>
#include <string>
#include <vector>

namespace TopoGunQtCompatibility {

inline bool isAmd64Image(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    IMAGE_DOS_HEADER dos{};
    DWORD read = 0;
    bool valid = ReadFile(file, &dos, sizeof(dos), &read, nullptr) &&
                 read == sizeof(dos) && dos.e_magic == IMAGE_DOS_SIGNATURE &&
                 dos.e_lfanew >= static_cast<LONG>(sizeof(dos));
    LARGE_INTEGER offset{};
    offset.QuadPart = valid ? dos.e_lfanew : 0;
    valid = valid && SetFilePointerEx(file, offset, nullptr, FILE_BEGIN);
    DWORD signature = 0;
    IMAGE_FILE_HEADER header{};
    if (valid) {
        valid = ReadFile(file, &signature, sizeof(signature), &read, nullptr) &&
                read == sizeof(signature) && signature == IMAGE_NT_SIGNATURE &&
                ReadFile(file, &header, sizeof(header), &read, nullptr) &&
                read == sizeof(header) &&
                header.Machine == IMAGE_FILE_MACHINE_AMD64;
    }
    CloseHandle(file);
    return valid;
}

inline DWORD fileVersion(const std::wstring& path, unsigned requiredMajor) {
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
    if (!size || size > 1024 * 1024) return 0;
    std::vector<BYTE> bytes(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, bytes.data())) return 0;
    VS_FIXEDFILEINFO* info = nullptr;
    UINT length = 0;
    if (!VerQueryValueW(bytes.data(), L"\\", reinterpret_cast<void**>(&info),
                        &length) ||
        length < sizeof(*info) || info->dwSignature != 0xfeef04bd) {
        return 0;
    }
    const DWORD major = HIWORD(info->dwFileVersionMS);
    const DWORD minor = LOWORD(info->dwFileVersionMS);
    const DWORD patch = HIWORD(info->dwFileVersionLS);
    return major == requiredMajor
        ? (major << 24) | (minor << 12) | patch : 0;
}

inline bool completeFamily(const std::wstring& root, unsigned major) {
    constexpr std::array<const wchar_t*, 3> modules = {
        L"Core.dll", L"Gui.dll", L"Widgets.dll"};
    const std::wstring prefix = root + L"\\Qt" + std::to_wstring(major);
    const DWORD version = fileVersion(prefix + L"Core.dll", major);
    if (!version || (major == 5 && version < ((5u << 24) | (12u << 12)))) return false;
    for (const wchar_t* module : modules) {
        const std::wstring path = prefix + module;
        if (!isAmd64Image(path) || fileVersion(path, major) != version) return false;
    }
    return true;
}

inline bool supportedRuntime(const char* version, unsigned requiredMajor) noexcept {
    return version && version[0] == static_cast<char>('0' + requiredMajor) &&
           version[1] == '.';
}

// Inspect imports without running the plugin or Qt initializers. Only the
// project's trusted plugin image is inspected here; host exports are queried.
inline std::wstring missingQtImport(const std::wstring& plugin,
                                    const std::wstring& root) {
    if (!isAmd64Image(plugin)) return L"汉化 DLL 不是有效的 x64 文件";
    HMODULE image = LoadLibraryExW(plugin.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (!image) return L"无法读取汉化 DLL";
    auto* base = reinterpret_cast<BYTE*>(image);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const auto directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    const DWORD rva = directory.VirtualAddress;
    const size_t imageSize = nt->OptionalHeader.SizeOfImage;
    auto inRange = [imageSize](ULONGLONG offset, size_t length) {
        return offset < imageSize && length <= imageSize - static_cast<size_t>(offset);
    };
    auto imageString = [&](ULONGLONG offset) -> const char* {
        if (!inRange(offset, 1)) return nullptr;
        const char* value = reinterpret_cast<const char*>(base + offset);
        return memchr(value, '\0', imageSize - static_cast<size_t>(offset)) ? value : nullptr;
    };
    std::wstring error;
    if (!rva || directory.Size < sizeof(IMAGE_IMPORT_DESCRIPTOR) ||
        !inRange(rva, directory.Size)) error = L"汉化 DLL 导入表无效";
    size_t descriptorOffset = rva;
    for (; error.empty(); descriptorOffset += sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
        if (descriptorOffset - rva > directory.Size - sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
            error = L"汉化 DLL 导入表缺少结束标记"; break;
        }
        auto* desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + descriptorOffset);
        if (!desc->Name) break;
        const char* name = imageString(desc->Name);
        if (!name) { error = L"汉化 DLL 模块名称无效"; break; }
        if (strncmp(name, "Qt5", 3) != 0) continue;
        if (strcmp(name, "Qt5Core.dll") && strcmp(name, "Qt5Gui.dll") &&
            strcmp(name, "Qt5Widgets.dll")) {
            error = L"汉化 DLL 请求了未支持的 Qt 模块"; break;
        }
        const std::string narrow(name);
        const std::wstring module(narrow.begin(), narrow.end());
        HMODULE qt = LoadLibraryExW((root + L"\\" + module).c_str(), nullptr,
                                    DONT_RESOLVE_DLL_REFERENCES);
        if (!qt) { error = L"无法读取 " + module; break; }
        if (!desc->OriginalFirstThunk) error = L"汉化 DLL 缺少原始导入表";
        size_t thunkOffset = desc->OriginalFirstThunk;
        for (; error.empty(); thunkOffset += sizeof(IMAGE_THUNK_DATA)) {
            if (!inRange(thunkOffset, sizeof(IMAGE_THUNK_DATA))) {
                error = L"汉化 DLL 接口表越界"; break;
            }
            auto* thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(base + thunkOffset);
            if (!thunk->u1.AddressOfData) break;
            const bool ordinal = IMAGE_SNAP_BY_ORDINAL(thunk->u1.Ordinal) != 0;
            const char* symbol = ordinal
                ? MAKEINTRESOURCEA(IMAGE_ORDINAL(thunk->u1.Ordinal))
                : (inRange(thunk->u1.AddressOfData, sizeof(WORD) + 1)
                    ? imageString(thunk->u1.AddressOfData + sizeof(WORD)) : nullptr);
            if (!symbol) { error = L"汉化 DLL 接口名称无效"; break; }
            if (!GetProcAddress(qt, symbol)) {
                error = module + L" 缺少接口：";
                if (ordinal) error += std::to_wstring(IMAGE_ORDINAL(thunk->u1.Ordinal));
                else { const std::string s(symbol); error.append(s.begin(), s.end()); }
            }
        }
        FreeLibrary(qt);
    }
    FreeLibrary(image);
    return error;
}

} // namespace TopoGunQtCompatibility
