#pragma once
#include "Settings/ShaderCatalog.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <string>

inline std::string ReadUserShaderFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        return {};
    const auto size = file.tellg();
    // RAVU Zoom AR R3 embeds a 5 MB weight table.
    if (size <= 0 || size > 16 * 1024 * 1024)
        return {};
    std::string source(static_cast<size_t>(size), '\0');
    file.seekg(0);
    if (!file.read(source.data(), size))
        return {};
    return source;
}

inline std::string LoadUserShaderSource(int id)
{
    if (id < 0 || id >= ShaderCount || !ShaderListed(id))
        return {};
    const auto& info = ShaderCatalog[id];
    if (info.parts)
    {
        // A chain from a package runs as one shader, its files in order.
        std::string joined;
        for (const auto& part : *info.parts)
        {
            const auto source = ReadUserShaderFile(part);
            if (source.empty())
                return {};
            joined += source + "\n";
        }
        return joined;
    }
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&LoadUserShaderSource),
                            &module))
        return {};
    wchar_t path[32768];
    const DWORD count = GetModuleFileNameW(module, path, 32768);
    if (!count || count >= 32768)
        return {};
    return ReadUserShaderFile(std::filesystem::path(path).parent_path() / L"shaders" / L"PlayKit" / info.file);
}
