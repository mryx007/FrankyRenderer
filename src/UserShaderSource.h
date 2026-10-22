#pragma once
#include "ShaderCatalog.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <string>

inline std::string LoadUserShaderSource(int id)
{
    if (id < 0 || id >= ShaderCount)
        return {};
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&LoadUserShaderSource),
                            &module))
        return {};
    wchar_t path[32768];
    const DWORD count = GetModuleFileNameW(module, path, 32768);
    if (!count || count >= 32768)
        return {};
    std::ifstream file(std::filesystem::path(path).parent_path() / L"shaders" / L"PlayKit" / ShaderCatalog[id].file,
                       std::ios::binary | std::ios::ate);
    if (!file)
        return {};
    const auto size = file.tellg();
    if (size <= 0 || size > 4 * 1024 * 1024)
        return {};
    std::string source(static_cast<size_t>(size), '\0');
    file.seekg(0);
    if (!file.read(source.data(), size))
        return {};
    return source;
}
