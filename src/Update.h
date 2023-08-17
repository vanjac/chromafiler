#pragma once
#include <common.h>

#include <memory>
#include <Windows.h>

namespace chromafiler {

struct UpdateInfo {
    DWORD version;
    bool isNewer;
    std::unique_ptr<char[]> url;
};

constexpr DWORD makeVersion(BYTE v1, BYTE v2, BYTE v3, BYTE v4) {
    return (v1 << 24) | (v2 << 16) | (v3 << 8) | v4;
}

DWORD checkUpdate(UpdateInfo *info);
void openUpdate(const UpdateInfo &info);

} // namespace
