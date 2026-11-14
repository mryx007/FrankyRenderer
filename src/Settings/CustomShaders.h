#pragma once
#include "Settings/DisplayProfiles.h"
#include "Settings/ShaderCatalog.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// mpv-format shaders from the shaders\Custom folder beside the renderer. Each
// file keeps its slot after the built-in shaders, so settings, profiles and
// the chain order stay with the file when others are added or removed.
namespace CustomShaders
{
struct Slot
{
    std::string file, parameterNames[ShaderParameterLimit];
    std::wstring label;
    std::vector<std::wstring> parts;
    std::vector<std::wstring> choices[ShaderParameterLimit];
    ShaderParameterInfo parameters[ShaderParameterLimit] = {};
    int count = 0;
    bool usable = false;
};

struct Table
{
    std::array<Slot, ShaderCustomCount> slots;
    std::array<ShaderInfo, ShaderCustomCount> infos;
    std::string signature;
};

inline std::string Utf8(const std::wstring& text)
{
    const int count = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size_t(count), '\0');
    if (count)
        WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), out.data(), count, nullptr, nullptr);
    return out;
}

inline std::wstring Wide(const std::string& text)
{
    const int count = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
    std::wstring out(size_t(count), L'\0');
    if (count)
        MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), out.data(), count);
    return out;
}

inline std::filesystem::path Folder()
{
    HMODULE module = nullptr;
    wchar_t path[32768];
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&Folder),
                            &module))
        return {};
    const DWORD count = GetModuleFileNameW(module, path, 32768);
    if (!count || count >= 32768)
        return {};
    return std::filesystem::path(path).parent_path() / L"shaders" / L"Custom";
}

inline std::string Trim(std::string text)
{
    const auto first = text.find_first_not_of(" \t\r");
    if (first == std::string::npos)
        return {};
    return text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
}

inline bool Number(const std::string& text, float& value)
{
    const auto end = text.data() + text.size();
    const char* start = text.data();
    if (start != end && *start == '+')
        ++start;
    float parsed = 0;
    const auto result = std::from_chars(start, end, parsed);
    if (result.ec != std::errc{})
        return false;
    value = parsed;
    return true;
}

inline bool Directive(const std::string& line, const char* name, std::string* rest = nullptr)
{
    const size_t length = strlen(name);
    if (line.compare(0, length, name) || (line.size() > length && line[length] != ' ' && line[length] != '\t'))
        return false;
    if (rest)
        *rest = Trim(line.substr(length));
    return true;
}

// Reads the //!HOOK and //!PARAM headers. libplacebo needs a GPU to parse a
// shader, and the menu must also work without a video.
inline void ReadHeaders(const std::string& text, Slot& slot)
{
    std::vector<std::string> lines;
    std::istringstream input(text);
    for (std::string line; std::getline(input, line);)
        lines.push_back(Trim(line));
    for (size_t i = 0; i < lines.size(); ++i)
    {
        slot.usable |= Directive(lines[i], "//!HOOK");
        std::string name;
        if (!Directive(lines[i], "//!PARAM", &name) || name.empty() || slot.count == ShaderParameterLimit)
            continue;
        bool integer = false, unsignedValue = false, isEnum = false, haveMinimum = false, haveMaximum = false;
        float minimum = 0, maximum = 0, initial = 0;
        size_t at = i + 1;
        for (; at < lines.size() && lines[at].rfind("//!", 0) == 0 && !Directive(lines[at], "//!PARAM") &&
               !Directive(lines[at], "//!HOOK");
             ++at)
        {
            std::string value;
            if (Directive(lines[at], "//!MINIMUM", &value))
                haveMinimum = Number(value, minimum);
            else if (Directive(lines[at], "//!MAXIMUM", &value))
                haveMaximum = Number(value, maximum);
            else if (Directive(lines[at], "//!TYPE", &value))
            {
                isEnum = value.find("ENUM") != std::string::npos;
                unsignedValue = value.find("uint") != std::string::npos;
                integer = isEnum || unsignedValue || value.find("int") != std::string::npos ||
                          value.find("DEFINE") != std::string::npos;
            }
        }
        const int index = slot.count;
        if (isEnum)
        {
            for (; at < lines.size() && !lines[at].empty() && lines[at].rfind("//!", 0) != 0; ++at)
                slot.choices[index].push_back(Wide(lines[at]));
            if (slot.choices[index].empty())
                continue;
            minimum = 0;
            maximum = float(slot.choices[index].size() - 1);
            initial = 0;
        }
        else
        {
            while (at < lines.size() && lines[at].empty())
                ++at;
            if (at >= lines.size() || lines[at].rfind("//!", 0) == 0 || !Number(lines[at], initial))
                continue;
            // A slider needs limits even when the author gave none.
            if (!haveMinimum)
                minimum = initial < 0 ? initial * 2 : 0;
            if (!haveMaximum)
                maximum = std::max({initial * 2, minimum + 1, integer ? 100.f : 1.f});
            if (unsignedValue)
                minimum = std::max(minimum, 0.f);
            if (minimum > maximum)
                continue;
            initial = std::clamp(initial, minimum, maximum);
        }
        slot.parameterNames[index] = name;
        slot.parameters[index] = {nullptr, initial, minimum, maximum, integer};
        ++slot.count;
    }
}

inline std::wstring SlotKey()
{
    return DisplayProfiles::Root() + L"\\CustomShaders";
}

inline std::map<std::wstring, int> LoadAssignments()
{
    std::map<std::wstring, int> saved;
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, SlotKey().c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS)
        return saved;
    for (DWORD i = 0;; ++i)
    {
        wchar_t name[512];
        DWORD length = DWORD(std::size(name)), type = 0, value = 0, bytes = sizeof(value);
        const auto result =
            RegEnumValueW(key, i, name, &length, nullptr, &type, reinterpret_cast<BYTE*>(&value), &bytes);
        if (result == ERROR_NO_MORE_ITEMS)
            break;
        if (result == ERROR_SUCCESS && type == REG_DWORD && value < DWORD(ShaderCustomCount))
            saved[name] = int(value);
    }
    RegCloseKey(key);
    return saved;
}

// Slots of deleted files stay reserved, because settings still point at them.
// They are given to new files only when no slot has been left unused.
inline std::map<std::wstring, int> Assign(const std::vector<std::wstring>& files)
{
    auto saved = LoadAssignments();
    std::map<std::wstring, int> result;
    std::set<int> taken;
    for (const auto& file : files)
        if (const auto found = saved.find(file); found != saved.end() && taken.insert(found->second).second)
            result[file] = found->second;
    HKEY key = nullptr;
    RegCreateKeyExW(HKEY_CURRENT_USER, SlotKey().c_str(), 0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &key, nullptr);
    for (const auto& file : files)
    {
        if (result.count(file))
            continue;
        std::set<int> reserved = taken;
        for (const auto& entry : saved)
            reserved.insert(entry.second);
        int slot = -1;
        for (int i = 0; i < ShaderCustomCount && slot < 0; ++i)
            if (!reserved.count(i))
                slot = i;
        for (auto entry = saved.begin(); slot < 0 && entry != saved.end(); ++entry)
            if (!taken.count(entry->second))
            {
                slot = entry->second;
                if (key)
                    RegDeleteValueW(key, entry->first.c_str());
                saved.erase(entry);
                break;
            }
        if (slot < 0)
            break;
        result[file] = slot;
        taken.insert(slot);
        saved[file] = slot;
        if (key)
        {
            const DWORD value = DWORD(slot);
            RegSetValueExW(key, file.c_str(), 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
        }
    }
    if (key)
        RegCloseKey(key);
    return result;
}

inline std::string Signature(const Table& table)
{
    std::ostringstream out;
    for (const auto& slot : table.slots)
    {
        out << slot.parts.size() << '|' << slot.file << '|' << slot.usable << '|' << slot.count;
        for (int i = 0; i < slot.count; ++i)
            out << '|' << slot.parameterNames[i] << ',' << slot.parameters[i].initial << ','
                << slot.parameters[i].minimum << ',' << slot.parameters[i].maximum << ',' << slot.parameters[i].integer
                << ',' << slot.choices[i].size();
        out << '\n';
    }
    return out.str();
}

// One line of the list: a shader file, or a chain of them from a package.
struct Entry
{
    std::wstring key, label;
    std::vector<std::wstring> parts;
};

inline std::wstring Lower(std::wstring text)
{
    std::transform(text.begin(), text.end(), text.begin(), towlower);
    return text;
}

inline std::string Quoted(const std::string& line, const char* marker)
{
    const auto at = line.find(marker);
    if (at == std::string::npos)
        return {};
    const auto first = at + strlen(marker), last = line.find('"', first);
    return last == std::string::npos ? std::string() : line.substr(first, last - first);
}

// mpv packages such as Anime4K list their chains in input.conf, one per key:
//   CTRL+1 change-list glsl-shaders set "~~/shaders/a.glsl;~~/shaders/b.glsl"; show-text "Mode A"
inline void ReadPackage(const std::filesystem::path& conf,
                        const std::filesystem::path& folder,
                        std::vector<Entry>& entries,
                        std::set<std::wstring>& members)
{
    const auto base = conf.parent_path();
    std::ifstream stream(conf);
    for (std::string line; std::getline(stream, line);)
    {
        line = Trim(line);
        const auto chain = Quoted(line, "glsl-shaders set \"");
        if (line.empty() || line[0] == '#' || chain.empty())
            continue;
        const auto name = Quoted(line, "show-text \"");
        Entry entry;
        entry.key = conf.lexically_relative(folder).wstring() + L"#" + Wide(name.empty() ? chain : name);
        entry.label = name.empty() ? base.filename().wstring() : Wide(name);
        std::istringstream list(chain);
        bool valid = true;
        for (std::string part; std::getline(list, part, ';');)
        {
            part = Trim(part);
            if (part.rfind("~~/", 0) == 0)
                part.erase(0, 3);
            if (part.empty() || part.find("..") != std::string::npos)
            {
                valid = false;
                continue;
            }
            const auto path = (base / Wide(part)).lexically_normal();
            members.insert(Lower(path.wstring()));
            entry.parts.push_back(path.wstring());
        }
        if (!valid)
            entry.parts.clear();
        entries.push_back(std::move(entry));
    }
}

inline std::vector<Entry> FindEntries(const std::filesystem::path& folder)
{
    namespace fs = std::filesystem;
    std::vector<fs::path> confs, shaders;
    std::error_code error;
    fs::recursive_directory_iterator walk(folder, fs::directory_options::skip_permission_denied, error), end;
    for (; !error && walk != end; walk.increment(error))
    {
        const auto path = walk->path();
        const auto name = Lower(path.filename().wstring());
        std::error_code fileError;
        if (walk->is_directory(fileError))
        {
            // Mac archives add a __MACOSX folder of metadata.
            if (name == L"__macosx" || walk.depth() >= 3)
                walk.disable_recursion_pending();
            continue;
        }
        const auto extension = Lower(path.extension().wstring());
        if (name.rfind(L"._", 0) == 0 || !walk->is_regular_file(fileError))
            continue;
        if (name == L"input.conf")
            confs.push_back(path);
        else if (extension == L".glsl" || extension == L".hook")
            shaders.push_back(path);
    }
    std::sort(confs.begin(), confs.end());
    std::sort(shaders.begin(), shaders.end());
    std::vector<Entry> entries;
    std::set<std::wstring> members;
    std::vector<std::wstring> packageFolders;
    for (const auto& conf : confs)
    {
        const auto before = entries.size();
        ReadPackage(conf, folder, entries, members);
        if (entries.size() != before)
            packageFolders.push_back(Lower((conf.parent_path() / L"shaders").wstring()) + L"\\");
    }
    for (const auto& shader : shaders)
    {
        // The single files of a package are parts of its chains.
        const auto lower = Lower(shader.lexically_normal().wstring());
        const bool inPackage = std::any_of(packageFolders.begin(),
                                           packageFolders.end(),
                                           [&](const std::wstring& dir) { return lower.rfind(dir, 0) == 0; });
        if (!inPackage && !members.count(lower))
            entries.push_back(
                {shader.lexically_relative(folder).wstring(), shader.stem().wstring(), {shader.wstring()}});
    }
    return entries;
}

// Looks at the folder again. Earlier tables stay alive: other threads may
// still hold names from them, and a table only changes with the folder.
inline void Refresh()
{
    static std::mutex lock;
    static std::vector<std::unique_ptr<Table>> kept;
    std::lock_guard guard(lock);
    try
    {
        // Deleting a built-in shader's file takes it off the list. Without the
        // folder at all the install is broken, and nothing is hidden.
        const auto builtin = Folder().parent_path() / L"PlayKit";
        std::error_code folderError;
        uint32_t missing = 0;
        if (std::filesystem::is_directory(builtin, folderError))
            for (int id = 0; id < ShaderBuiltinCount; ++id)
            {
                std::error_code fileError;
                if (!std::filesystem::exists(builtin / BuiltinShaderCatalog[id].file, fileError))
                    missing |= 1u << id;
            }
        BuiltinShadersMissing.store(missing);
        const auto entries = FindEntries(Folder());
        std::vector<std::wstring> keys;
        for (const auto& entry : entries)
            keys.push_back(entry.key);
        const auto assigned = Assign(keys);
        auto next = std::make_unique<Table>();
        for (auto& info : next->infos)
            info = EmptyShaderSlot;
        std::set<std::wstring> labels;
        for (int id = 0; id < ShaderBuiltinCount; ++id)
            labels.insert(BuiltinShaderCatalog[id].label);
        for (const auto& entry : entries)
        {
            const auto found = assigned.find(entry.key);
            if (found == assigned.end())
                continue;
            auto& slot = next->slots[size_t(found->second)];
            slot.file = Utf8(entry.key);
            slot.parts = entry.parts;
            bool complete = !entry.parts.empty();
            for (const auto& part : entry.parts)
            {
                // The headers are at the top; shaders can embed megabytes of weights.
                std::ifstream stream(part.c_str(), std::ios::binary);
                complete = complete && stream.good();
                std::string text(256 * 1024, '\0');
                stream.read(text.data(), std::streamsize(text.size()));
                text.resize(size_t(stream.gcount()));
                ReadHeaders(text, slot);
            }
            slot.usable = slot.usable && complete;
            // The menu finds a shader's card by its title.
            slot.label = entry.label;
            while (!labels.insert(slot.usable ? slot.label : slot.label + L" (not compatible)").second)
                slot.label += L" (Custom)";
            if (!slot.usable)
            {
                slot.label += L" (not compatible)";
                slot.count = 0;
            }
            for (int i = 0; i < slot.count; ++i)
                slot.parameters[i].name = slot.parameterNames[i].c_str();
            auto& info = next->infos[size_t(found->second)];
            info = EmptyShaderSlot;
            info.file = slot.file.c_str();
            info.label = slot.label.c_str();
            info.category = L"Custom";
            info.help = L"From the shaders\\Custom folder beside the renderer.";
            info.count = slot.count;
            std::copy(std::begin(slot.parameters), std::end(slot.parameters), info.parameters);
            info.parts = &slot.parts;
            info.choices = slot.choices;
            info.usable = slot.usable;
        }
        next->signature = Signature(*next);
        if (!kept.empty() && kept.back()->signature == next->signature)
            return;
        CustomShaderSlots.store(next->infos.data());
        kept.push_back(std::move(next));
    }
    catch (...)
    {
        // Custom shaders are optional; playback and the menu go on without them.
    }
}
}
