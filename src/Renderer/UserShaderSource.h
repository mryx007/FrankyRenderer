#pragma once
#include "Settings/ShaderCatalog.h"
#include <windows.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <vector>

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

struct ShaderFileTraits
{
    int stage = ShaderStageUnknown;
    bool scales = false;
};

// A shader's stage and whether it scales, read from its file once
// (Settings/ShaderCatalog.h).
inline ShaderFileTraits ShaderTraits(int id)
{
    if (id < 0 || id >= ShaderCount || !ShaderListed(id))
        return {};
    const auto& info = ShaderCatalog[id];
    std::wstring key = std::to_wstring(id) + L"|" + info.label;
    if (info.parts)
        for (const auto& part : *info.parts)
            key += L"|" + part;
    static std::mutex lock;
    static std::map<std::wstring, ShaderFileTraits> known;
    {
        std::lock_guard guard(lock);
        if (const auto found = known.find(key); found != known.end())
            return found->second;
    }
    const auto source = LoadUserShaderSource(id);
    const ShaderFileTraits traits{ShaderStageOfSource(source), ShaderScalesOfSource(source)};
    std::lock_guard guard(lock);
    known[key] = traits;
    return traits;
}

inline int ShaderStage(int id)
{
    return ShaderTraits(id).stage;
}

// Whether Automatic Order places this shader. A custom shader takes part only
// when its file clearly scales; its purpose is not guessed from names.
inline bool ShaderAutoSorted(int id)
{
    return id >= 0 && (id < ShaderBuiltinCount || ShaderTraits(id).scales);
}

// The automatic order of the switched-on shaders, given in stored order:
// sorted ones by key (stage, then cleanup, scaling, sharpening), ties in
// stored order; the others keep their positions in the chain.
template <class Key, class Sorted>
std::vector<int> AutomaticShaderChain(std::vector<int> active, Key key, Sorted sorted)
{
    std::vector<int> moving;
    for (int id : active)
        if (sorted(id))
            moving.push_back(id);
    std::stable_sort(moving.begin(), moving.end(), [&](int a, int b) { return key(a) < key(b); });
    size_t next = 0;
    for (auto& id : active)
        if (sorted(id))
            id = moving[next++];
    return active;
}

// The order the shaders run in. Switched-off shaders keep their places, so
// their files are not read.
inline std::vector<int> ShaderChainOrder(const UserShaderSettings& s, bool automatic)
{
    std::vector<int> order(std::begin(s.order), std::end(s.order));
    if (!automatic)
        return order;
    std::vector<size_t> places;
    std::vector<int> active;
    for (size_t i = 0; i < order.size(); i++)
        if (s.shaders[order[i]].enabled && ShaderListed(order[i]))
        {
            places.push_back(i);
            active.push_back(order[i]);
        }
    active = AutomaticShaderChain(
        active, [](int id) { return std::pair{ShaderStage(id), ShaderRole(id)}; }, ShaderAutoSorted);
    for (size_t i = 0; i < places.size(); i++)
        order[places[i]] = active[i];
    return order;
}
