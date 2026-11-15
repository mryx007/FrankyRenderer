#pragma once
#include <windows.h>
#include <atomic>
#include <charconv>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#pragma comment(lib, "advapi32.lib")

// Settings are kept in %APPDATA%\FrankyRenderer\Settings.ini
// once anything has been saved. Until then they are read from the registry as in
// earlier versions. The first change copies the registry settings into the
// file and moves the registry key to <root>.Backup, so deleting the file
// restores the defaults. The crash notes of running players (Pending*,
// DiagnosticLogNextStart) stay in the registry.
//
// Paths are the registry paths below HKEY_CURRENT_USER, starting with Root().
// Names compare without case, as in the registry.
namespace SettingsStore
{
inline std::wstring Root()
{
    wchar_t test[512];
    const auto size = GetEnvironmentVariableW(L"PLACEBO_RENDERER_TEST_ROOT", test, 512);
    return size && size < 512 ? test : L"Software\\PlaceboRenderer";
}

inline std::wstring IniPath()
{
    wchar_t value[MAX_PATH];
    if (GetEnvironmentVariableW(L"PLACEBO_RENDERER_TEST_ROOT", value, MAX_PATH))
    {
        // Tests never use the user's file, only one they name themselves.
        const auto size = GetEnvironmentVariableW(L"PLACEBO_RENDERER_TEST_INI", value, MAX_PATH);
        return size && size < MAX_PATH ? value : L"";
    }
    const auto size = GetEnvironmentVariableW(L"APPDATA", value, MAX_PATH);
    return size && size < MAX_PATH ? std::wstring(value) + L"\\FrankyRenderer\\Settings.ini" : L"";
}

inline std::wstring Folder()
{
    const auto path = IniPath();
    const auto slash = path.find_last_of(L'\\');
    return slash == path.npos ? L"" : path.substr(0, slash);
}

struct Less
{
    bool operator()(const std::wstring& a, const std::wstring& b) const
    {
        return CompareStringOrdinal(a.c_str(), int(a.size()), b.c_str(), int(b.size()), TRUE) == CSTR_LESS_THAN;
    }
};

struct Value
{
    DWORD type = REG_NONE;
    std::vector<BYTE> data;
    bool operator==(const Value&) const = default;
};

struct Node
{
    std::map<std::wstring, Value, Less> values;
    std::map<std::wstring, Node, Less> keys;

    bool Empty() const
    {
        return values.empty() && keys.empty();
    }
};

// Crash notes of running players; see DisplayModes, WindowsHdrSwitch,
// SettingsJournal and Diagnostics.
inline bool RuntimeValue(const std::wstring& name)
{
    return !_wcsnicmp(name.c_str(), L"Pending", 7) || !_wcsicmp(name.c_str(), L"DiagnosticLogNextStart");
}

enum class OpKind
{
    Set,
    DeleteValue,
    DeleteTree,
    CreateKey
};

struct Op
{
    OpKind kind;
    std::wstring path, name; // path below Root()
    Value value;
};

inline Node* Find(Node& root, const std::wstring& path, bool create, bool* created = nullptr)
{
    Node* node = &root;
    size_t start = 0;
    while (start < path.size())
    {
        auto end = path.find(L'\\', start);
        if (end == path.npos)
            end = path.size();
        if (end > start)
        {
            const auto part = path.substr(start, end - start);
            auto found = node->keys.find(part);
            if (found == node->keys.end())
            {
                if (!create)
                    return nullptr;
                found = node->keys.emplace(part, Node{}).first;
                if (created)
                    *created = true;
            }
            node = &found->second;
        }
        start = end + 1;
    }
    return node;
}

inline const Node* Find(const Node& root, const std::wstring& path)
{
    return Find(const_cast<Node&>(root), path, false);
}

// Returns whether the tree changed.
inline bool Apply(Node& root, const Op& op)
{
    if (op.kind == OpKind::DeleteTree)
    {
        if (op.path.empty())
        {
            const bool changed = !root.Empty();
            root = {};
            return changed;
        }
        const auto slash = op.path.find_last_of(L'\\');
        Node* parent = slash == op.path.npos ? &root : Find(root, op.path.substr(0, slash), false);
        return parent && parent->keys.erase(slash == op.path.npos ? op.path : op.path.substr(slash + 1));
    }
    if (op.kind == OpKind::DeleteValue)
    {
        Node* node = Find(root, op.path, false);
        return node && node->values.erase(op.name);
    }
    bool created = false;
    Node* node = Find(root, op.path, true, &created);
    if (op.kind == OpKind::CreateKey)
        return created;
    const auto found = node->values.find(op.name);
    if (found != node->values.end() && found->second == op.value)
        return created;
    node->values[op.name] = op.value;
    return true;
}

// --- Text form ---------------------------------------------------------------

inline bool Utf8(const std::wstring& text, std::string& out)
{
    out.clear();
    if (text.empty())
        return true;
    const int count =
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
    if (!count)
        return false;
    out.resize(count);
    WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), int(text.size()), out.data(), count, nullptr, nullptr);
    return true;
}

inline bool Wide(const std::string& text, std::wstring& out)
{
    out.clear();
    if (text.empty())
        return true;
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), nullptr, 0);
    if (!count)
        return false;
    out.resize(count);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), out.data(), count);
    return true;
}

inline std::string Quote(const std::string& value)
{
    std::string out = "\"";
    for (char ch : value)
    {
        if (ch == '\\' || ch == '"')
            out += '\\';
        if (ch == '\n')
            out += "\\n";
        else if (ch == '\r')
            out += "\\r";
        else if (ch == '\t')
            out += "\\t";
        else
            out += ch;
    }
    return out + '"';
}

inline bool Unquote(const std::string& value, std::string& out)
{
    out.clear();
    if (value.size() < 2 || value.front() != '"' || value.back() != '"')
        return false;
    for (size_t i = 1; i + 1 < value.size(); ++i)
    {
        char ch = value[i];
        if (ch == '"')
            return false;
        if (ch == '\\')
        {
            if (++i + 1 >= value.size())
                return false;
            ch = value[i];
            if (ch == 'n')
                ch = '\n';
            else if (ch == 'r')
                ch = '\r';
            else if (ch == 't')
                ch = '\t';
            else if (ch != '\\' && ch != '"')
                return false;
        }
        out += ch;
    }
    return true;
}

// The text of a string value, when it is one without hidden bytes.
inline bool PlainString(const Value& value, std::wstring& text)
{
    const auto& data = value.data;
    if (value.type != REG_SZ || data.size() < 2 || data.size() % 2)
        return false;
    text.assign(reinterpret_cast<const wchar_t*>(data.data()), data.size() / 2);
    if (text.back())
        return false;
    text.pop_back();
    return text.find(L'\0') == text.npos;
}

inline std::string NameText(const std::wstring& name)
{
    std::string text;
    if (!Utf8(name, text))
        text.clear();
    const bool plain = !name.empty() && name.front() != L' ' && name.back() != L' ' &&
                       name.find_first_not_of(L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 ._-()+,"
                                              L"&'!~@$%{}") == name.npos;
    return plain ? text : Quote(text);
}

// The shortest text that reads back to the same float, always with a point
// or exponent, so it is told apart from a whole number.
inline std::string FloatText(float number)
{
    char buffer[48];
    const auto written = std::to_chars(buffer, buffer + sizeof(buffer), number);
    std::string text(buffer, written.ptr);
    if (text.find_first_of(".eEin") == text.npos)
        text += ".0";
    return text;
}

// A number written by FloatText or by hand ("0.5", "2.0", "1e-3").
inline bool ParseFloat(const std::string& text, float& number)
{
    const auto first = text.data(), last = text.data() + text.size();
    const auto result = std::from_chars(first, last, number);
    return !text.empty() && result.ec == std::errc{} && result.ptr == last && std::isfinite(number);
}

inline bool IsFloatText(const std::string& text)
{
    return text.find_first_of(".eE") != text.npos;
}

inline std::string ValueText(const Value& value)
{
    if (value.type == REG_DWORD && value.data.size() == 4)
    {
        int number;
        memcpy(&number, value.data.data(), 4);
        return std::to_string(number);
    }
    std::wstring wide;
    std::string text;
    if (PlainString(value, wide) && Utf8(wide, text))
        return Quote(text);
    if (value.type == REG_BINARY && value.data.size() == 4)
    {
        float number, back;
        memcpy(&number, value.data.data(), 4);
        if (std::isfinite(number))
            if (const auto written = FloatText(number); ParseFloat(written, back) && !memcmp(&back, &number, 4))
                return written;
    }
    text = value.type == REG_BINARY ? "hex:" : "hex(" + std::to_string(value.type) + "):";
    static constexpr char digits[] = "0123456789abcdef";
    for (BYTE byte : value.data)
    {
        text += digits[byte >> 4];
        text += digits[byte & 15];
    }
    return text;
}

inline bool ParseValue(const std::string& text, Value& value)
{
    value = {};
    if (text.starts_with('"'))
    {
        std::string raw;
        std::wstring wide;
        if (!Unquote(text, raw) || !Wide(raw, wide) || wide.find(L'\0') != wide.npos)
            return false;
        value.type = REG_SZ;
        const auto bytes = reinterpret_cast<const BYTE*>(wide.c_str());
        value.data.assign(bytes, bytes + (wide.size() + 1) * sizeof(wchar_t));
        return true;
    }
    if (text.starts_with("float:") || (IsFloatText(text) && !text.starts_with("hex")))
    {
        float number = 0;
        if (!ParseFloat(text.starts_with("float:") ? text.substr(6) : text, number))
            return false;
        value.type = REG_BINARY;
        value.data.resize(4);
        memcpy(value.data.data(), &number, 4);
        return true;
    }
    if (text.starts_with("hex"))
    {
        size_t at = 3;
        value.type = REG_BINARY;
        if (at < text.size() && text[at] == '(')
        {
            const auto close = text.find(')', at);
            unsigned long type = 0;
            if (close == text.npos ||
                std::from_chars(text.data() + at + 1, text.data() + close, type).ptr != text.data() + close)
                return false;
            value.type = DWORD(type);
            at = close + 1;
        }
        if (at >= text.size() || text[at] != ':' || (text.size() - at - 1) % 2)
            return false;
        auto nibble = [](char ch)
        {
            return ch >= '0' && ch <= '9'   ? ch - '0'
                   : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10
                   : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10
                                            : -1;
        };
        for (size_t i = at + 1; i < text.size(); i += 2)
        {
            const int high = nibble(text[i]), low = nibble(text[i + 1]);
            if (high < 0 || low < 0)
                return false;
            value.data.push_back(BYTE(high << 4 | low));
        }
        return true;
    }
    long long number = 0;
    const auto last = text.data() + text.size();
    const auto result = std::from_chars(text.data(), last, number);
    if (text.empty() || result.ec != std::errc{} || result.ptr != last || number < -2147483648LL ||
        number > 4294967295LL)
        return false;
    const DWORD stored = DWORD(number);
    value.type = REG_DWORD;
    value.data.resize(4);
    memcpy(value.data.data(), &stored, 4);
    return true;
}

// A block of settings kept as one binary value (a structure) is written as
// one line per field, "<prefix>.<field>=<text>", and put together again when
// read. Settings.cpp registers them; it knows the structures. Fields missing
// from the file keep their defaults. A field named ";" is a comment line.
struct Codec
{
    std::wstring name;                                   // the binary value, e.g. ScalingV1
    std::string prefix;                                  // empty: every field name with a point, without prefix
    size_t size = 0;                                     // 0: any size
    std::function<bool(const std::wstring& path)> where; // keys the value is found in
    std::function<void(const std::vector<BYTE>& data, std::vector<std::pair<std::string, std::string>>& fields)> expand;
    std::function<void(const std::map<std::string, std::string>& fields, std::vector<BYTE>& data)> collapse;
    // Clears bytes no field holds (after the end of a text), before comparing.
    std::function<void(std::vector<BYTE>& data)> clean;
};

inline std::vector<Codec>& Codecs()
{
    static std::vector<Codec> codecs;
    return codecs;
}

inline const Codec* CodecFor(const std::wstring& path, const std::wstring& name, const Value& value)
{
    for (const auto& codec : Codecs())
        if (!_wcsicmp(codec.name.c_str(), name.c_str()) && value.type == REG_BINARY &&
            (!codec.size || value.data.size() == codec.size) && codec.where(path))
            return &codec;
    return nullptr;
}

// Earlier layouts of a value that a newer one in the same key replaces.
inline bool Replaced(const Node& node, const std::wstring& name)
{
    for (const wchar_t* old : {L"UserShadersV1", L"UserShadersV2", L"UserShadersV3"})
        if (!_wcsicmp(name.c_str(), old))
            return node.values.count(L"UserShadersV4") != 0;
    return false;
}

inline constexpr char RootSection[] = "Settings";

inline void WriteNode(const Node& node, const std::wstring& path, std::string& out)
{
    std::string section;
    Utf8(path, section);
    out += "\r\n[" + std::string(RootSection) + (path.empty() ? "" : "\\" + section) + "]\r\n";
    for (const auto& [name, value] : node.values)
    {
        if (Replaced(node, name))
            continue;
        if (const auto* codec = CodecFor(path, name, value))
        {
            // Only when the fields give back exactly the same bytes.
            std::vector<std::pair<std::string, std::string>> fields;
            codec->expand(value.data, fields);
            std::map<std::string, std::string> map;
            for (const auto& [field, text] : fields)
                if (field != ";")
                    map[field] = text;
            std::vector<BYTE> back, cleaned = value.data;
            codec->collapse(map, back);
            codec->clean(cleaned);
            if (back == cleaned)
            {
                for (const auto& [field, text] : fields)
                    out += field == ";"            ? "; " + text + "\r\n"
                           : codec->prefix.empty() ? field + "=" + text + "\r\n"
                                                   : codec->prefix + "." + field + "=" + text + "\r\n";
                continue;
            }
        }
        out += NameText(name) + "=" + ValueText(value) + "\r\n";
    }
    for (const auto& [name, child] : node.keys)
        WriteNode(child, path.empty() ? name : path + L"\\" + name, out);
}

inline std::string Serialize(const Node& root)
{
    std::string out = "; FrankyRenderer settings (UTF-8). Delete this file to restore the defaults.\r\n"
                      "[FrankyRenderer]\r\nStorage=1\r\n";
    WriteNode(root, L"", out);
    return out;
}

inline std::string Trim(const std::string& text)
{
    const auto first = text.find_first_not_of(" \t\r");
    return first == text.npos ? std::string{} : text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
}

// Unreadable lines are skipped, so one bad hand edit loses only that value.
// Fails only for a file of a newer, unknown layout.
inline bool Parse(const std::string& text, Node& root)
{
    root = {};
    Node* section = nullptr;
    std::wstring sectionPath;
    bool header = false;
    // Field lines of the section, put together when it ends.
    std::map<const Codec*, std::map<std::string, std::string>> blocks;
    auto finish = [&]
    {
        for (const auto& [codec, fields] : blocks)
        {
            Value value{REG_BINARY, {}};
            codec->collapse(fields, value.data);
            if (!codec->size || value.data.size() == codec->size)
                section->values[codec->name] = std::move(value);
        }
        blocks.clear();
    };
    size_t at = text.starts_with("\xef\xbb\xbf") ? 3 : 0;
    while (at < text.size())
    {
        auto end = text.find('\n', at);
        if (end == text.npos)
            end = text.size();
        const auto line = Trim(text.substr(at, end - at));
        at = end + 1;
        if (line.empty() || line[0] == ';' || line[0] == '#')
            continue;
        if (line[0] == '[' && line.back() == ']')
        {
            const auto name = line.substr(1, line.size() - 2);
            std::wstring path;
            if (section)
                finish();
            header = name == "FrankyRenderer";
            section = nullptr;
            sectionPath.clear();
            if (name == RootSection)
                section = &root;
            else if (name.starts_with(std::string(RootSection) + "\\") && Wide(name.substr(9), path))
            {
                section = Find(root, path, true);
                sectionPath = path;
            }
            continue;
        }
        std::string nameText, valueText;
        if (line[0] == '"')
        {
            size_t close = 1;
            while (close < line.size() && line[close] != '"')
                close += line[close] == '\\' ? 2 : 1;
            if (close >= line.size())
                continue;
            nameText = line.substr(0, close + 1);
            const auto rest = Trim(line.substr(close + 1));
            if (rest.empty() || rest[0] != '=')
                continue;
            valueText = Trim(rest.substr(1));
            std::string raw;
            if (!Unquote(nameText, raw))
                continue;
            nameText = raw;
        }
        else
        {
            const auto equals = line.find('=');
            if (equals == line.npos || !equals)
                continue;
            nameText = Trim(line.substr(0, equals));
            valueText = Trim(line.substr(equals + 1));
        }
        if (header)
        {
            if (nameText == "Storage" && valueText != "1")
                return false;
            continue;
        }
        if (!section)
            continue;
        const Codec* block = nullptr;
        for (const auto& codec : Codecs())
            if ((codec.prefix.empty()
                     ? nameText.find('.') != nameText.npos
                     : nameText.size() > codec.prefix.size() + 1 && nameText.starts_with(codec.prefix + ".")) &&
                codec.where(sectionPath))
                block = &codec;
        if (block)
        {
            blocks[block][block->prefix.empty() ? nameText : nameText.substr(block->prefix.size() + 1)] = valueText;
            continue;
        }
        std::wstring name;
        Value value;
        if (Wide(nameText, name) && ParseValue(valueText, value))
            section->values[name] = std::move(value);
    }
    if (section)
        finish();
    return true;
}

// --- File -------------------------------------------------------------------

inline bool Stamp(const std::wstring& path, ULONGLONG& time, ULONGLONG& size)
{
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
        return false;
    time = ULONGLONG(data.ftLastWriteTime.dwHighDateTime) << 32 | data.ftLastWriteTime.dwLowDateTime;
    size = ULONGLONG(data.nFileSizeHigh) << 32 | data.nFileSizeLow;
    return true;
}

inline bool ReadFileText(const std::wstring& path, std::string& text)
{
    for (int attempt = 0; attempt < 5; ++attempt)
    {
        const HANDLE file = CreateFileW(path.c_str(),
                                        GENERIC_READ,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                        nullptr,
                                        OPEN_EXISTING,
                                        FILE_ATTRIBUTE_NORMAL,
                                        nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            if (GetLastError() != ERROR_SHARING_VIOLATION)
                return false;
            Sleep(10);
            continue;
        }
        LARGE_INTEGER size = {};
        bool ok = GetFileSizeEx(file, &size) && size.QuadPart < 64 * 1024 * 1024;
        if (ok)
        {
            text.resize(size_t(size.QuadPart));
            DWORD read = 0;
            ok = text.empty() ||
                 (ReadFile(file, text.data(), DWORD(text.size()), &read, nullptr) && read == text.size());
        }
        CloseHandle(file);
        return ok;
    }
    return false;
}

inline bool WriteFileText(const std::wstring& path, const std::string& text)
{
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), error);
    const auto temporary = path + L".tmp-" + std::to_wstring(GetCurrentProcessId());
    const HANDLE file =
        CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    DWORD written = 0;
    const bool saved = WriteFile(file, text.data(), DWORD(text.size()), &written, nullptr) && written == text.size();
    CloseHandle(file);
    // A scanner or editor may hold the file for a moment.
    for (int attempt = 0; saved && attempt < 25; ++attempt)
    {
        if (MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
            return true;
        Sleep(10);
    }
    DeleteFileW(temporary.c_str());
    return false;
}

// Serializes the file between player processes.
class FileLock
{
  public:
    FileLock() : m_mutex(CreateMutexW(nullptr, FALSE, L"Local\\FrankyRendererSettingsFile"))
    {
        if (m_mutex)
        {
            const auto result = WaitForSingleObject(m_mutex, 3000);
            m_owned = result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
        }
    }

    ~FileLock()
    {
        if (m_owned)
            ReleaseMutex(m_mutex);
        if (m_mutex)
            CloseHandle(m_mutex);
    }

    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;

  private:
    HANDLE m_mutex;
    bool m_owned = false;
};

// --- Registry (before the first change, and when no file can be written) -----

inline std::wstring Full(const std::wstring& path)
{
    return path.empty() ? Root() : Root() + L"\\" + path;
}

inline void ReadRegistry(HKEY key, Node& node, bool top)
{
    for (DWORD i = 0;; ++i)
    {
        wchar_t name[16384];
        DWORD length = DWORD(std::size(name)), type = 0, bytes = 0;
        const auto result = RegEnumValueW(key, i, name, &length, nullptr, &type, nullptr, &bytes);
        if (result == ERROR_NO_MORE_ITEMS)
            break;
        if (result != ERROR_SUCCESS || (top && RuntimeValue(name)))
            continue;
        Value value{type, std::vector<BYTE>(bytes)};
        DWORD size = bytes;
        if (RegQueryValueExW(key, name, nullptr, &type, value.data.data(), &size) == ERROR_SUCCESS)
        {
            value.type = type;
            value.data.resize(size);
            node.values[name] = std::move(value);
        }
    }
    for (DWORD i = 0;; ++i)
    {
        wchar_t name[256];
        DWORD length = DWORD(std::size(name));
        if (RegEnumKeyExW(key, i, name, &length, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
            break;
        HKEY child;
        if (RegOpenKeyExW(key, name, 0, KEY_READ, &child) == ERROR_SUCCESS)
        {
            ReadRegistry(child, node.keys[name], false);
            RegCloseKey(child);
        }
    }
}

inline Node RegistrySettings()
{
    Node root;
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, Root().c_str(), 0, KEY_READ, &key) == ERROR_SUCCESS)
    {
        ReadRegistry(key, root, true);
        RegCloseKey(key);
    }
    return root;
}

// After the file holds the settings: keep a copy as <root>.Backup and leave
// only the crash notes, so deleting the file gives the defaults.
inline void RetireRegistry()
{
    const auto root = Root();
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, root.c_str(), 0, KEY_READ | KEY_WRITE, &key) != ERROR_SUCCESS)
        return;
    const auto backupPath = root + L".Backup";
    RegDeleteTreeW(HKEY_CURRENT_USER, backupPath.c_str());
    HKEY backup;
    bool copied = false;
    if (RegCreateKeyExW(
            HKEY_CURRENT_USER, backupPath.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &backup, nullptr) ==
        ERROR_SUCCESS)
    {
        copied = RegCopyTreeW(key, nullptr, backup) == ERROR_SUCCESS;
        RegCloseKey(backup);
    }
    if (copied)
    {
        std::vector<std::wstring> keys, values;
        for (DWORD i = 0;; ++i)
        {
            wchar_t name[256];
            DWORD length = DWORD(std::size(name));
            if (RegEnumKeyExW(key, i, name, &length, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
                break;
            keys.push_back(name);
        }
        for (DWORD i = 0;; ++i)
        {
            wchar_t name[16384];
            DWORD length = DWORD(std::size(name));
            const auto result = RegEnumValueW(key, i, name, &length, nullptr, nullptr, nullptr, nullptr);
            if (result == ERROR_NO_MORE_ITEMS)
                break;
            if (result == ERROR_SUCCESS && !RuntimeValue(name))
                values.push_back(name);
        }
        for (const auto& name : keys)
            RegDeleteTreeW(key, name.c_str());
        for (const auto& name : values)
            RegDeleteValueW(key, name.c_str());
    }
    RegCloseKey(key);
}

inline bool RegistryHas(const Op& op)
{
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, Full(op.path).c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS)
        return false;
    bool found = true;
    if (op.kind == OpKind::Set || op.kind == OpKind::DeleteValue)
    {
        DWORD type = 0, bytes = 0;
        found = RegQueryValueExW(key, op.name.c_str(), nullptr, &type, nullptr, &bytes) == ERROR_SUCCESS;
        if (found && op.kind == OpKind::Set)
        {
            Value value{type, std::vector<BYTE>(bytes)};
            found =
                RegQueryValueExW(key, op.name.c_str(), nullptr, &type, value.data.data(), &bytes) == ERROR_SUCCESS &&
                value == op.value;
        }
    }
    RegCloseKey(key);
    return found;
}

// Whether the change would alter the registry.
inline bool RegistryChanges(const Op& op)
{
    const bool has = RegistryHas(op);
    return op.kind == OpKind::DeleteValue || op.kind == OpKind::DeleteTree ? has : !has;
}

inline LONG RegistryApply(const Op& op)
{
    const auto full = Full(op.path);
    if (op.kind == OpKind::DeleteTree)
    {
        const auto result = RegDeleteTreeW(HKEY_CURRENT_USER, full.c_str());
        return result == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : result;
    }
    if (op.kind == OpKind::DeleteValue)
    {
        const auto result = RegDeleteKeyValueW(HKEY_CURRENT_USER, full.c_str(), op.name.c_str());
        return result == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : result;
    }
    HKEY key;
    auto result =
        RegCreateKeyExW(HKEY_CURRENT_USER, full.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (result != ERROR_SUCCESS || op.kind == OpKind::CreateKey)
    {
        if (result == ERROR_SUCCESS)
            RegCloseKey(key);
        return result;
    }
    result = RegSetValueExW(key, op.name.c_str(), 0, op.value.type, op.value.data.data(), DWORD(op.value.data.size()));
    RegCloseKey(key);
    return result;
}

// --- Shared state -------------------------------------------------------------

struct State
{
    std::recursive_mutex lock;
    std::wstring path;       // the file this state belongs to
    bool ini = false;        // settings come from the file (tree)
    bool unreadable = false; // the file exists but could not be read: never overwrite it
    ULONGLONG time = 0, size = 0, checkedAt = 0, writtenAt = 0, failedAt = 0;
    Node tree;
    std::vector<Op> pending; // in the tree, not yet in the file
    int batch = 0;
};

inline State& Shared()
{
    static State state;
    return state;
}

// Changes whenever the settings were changed outside this process: the file
// was edited, written by another player, or deleted.
inline std::atomic<unsigned> Generation{0};

inline bool Flush(State& s);

// Whether the file is in use. Looks at the file at most every 100 ms.
inline bool Refresh(State& s, bool force = false)
{
    const auto now = GetTickCount64();
    const auto path = IniPath();
    if (path == s.path && !force && now - s.checkedAt < 100)
        return s.ini;
    s.checkedAt = now;
    if (path != s.path)
    {
        s.path = path;
        ++Generation;
        s.ini = s.unreadable = false;
        s.tree = {};
        s.pending.clear();
        s.time = s.size = 0;
    }
    ULONGLONG time = 0, size = 0;
    if (path.empty() || !Stamp(path, time, size))
    {
        // Changes still waiting create the file again (from the defaults).
        if (!path.empty() && !s.pending.empty())
        {
            // A file being created waits for the end of its batch.
            if (!(s.batch && !s.time))
                Flush(s);
            return s.ini;
        }
        // Deleted: the defaults (or the registry of an older version) apply.
        if (s.ini)
            ++Generation;
        s.ini = s.unreadable = false;
        s.tree = {};
        s.pending.clear();
        return false;
    }
    if (s.ini && !s.unreadable && time == s.time && size == s.size)
        return true;
    std::string text;
    Node tree;
    s.ini = true;
    if (!ReadFileText(path, text) || !Parse(text, tree))
    {
        s.unreadable = true;
        return true;
    }
    for (const auto& op : s.pending)
        Apply(tree, op);
    s.tree = std::move(tree);
    s.unreadable = false;
    s.time = time;
    s.size = size;
    ++Generation;
    return true;
}

// Writes the pending changes. Without a file, it is created from the registry
// settings; when that is impossible, the changes go to the registry instead.
inline bool Flush(State& s)
{
    if (s.pending.empty() || s.path.empty())
        return true;
    FileLock lock;
    Node tree;
    bool created = false;
    ULONGLONG time = 0, size = 0;
    if (Stamp(s.path, time, size))
    {
        if (s.unreadable || time != s.time || size != s.size)
        {
            std::string text;
            if (!ReadFileText(s.path, text) || !Parse(text, tree))
                return false;
            for (const auto& op : s.pending)
                Apply(tree, op);
            ++Generation;
        }
        else
            tree = s.tree;
    }
    else
    {
        tree = RegistrySettings();
        created = !tree.Empty();
        for (const auto& op : s.pending)
            Apply(tree, op);
    }
    if (!WriteFileText(s.path, Serialize(tree)))
    {
        if (Stamp(s.path, time, size))
            return false;
        // No file can be made here: keep using the registry.
        for (const auto& op : s.pending)
            RegistryApply(op);
        s.pending.clear();
        s.ini = false;
        s.tree = {};
        s.failedAt = GetTickCount64();
        return true;
    }
    if (created)
        RetireRegistry();
    s.tree = std::move(tree);
    s.pending.clear();
    s.ini = true;
    s.unreadable = false;
    Stamp(s.path, s.time, s.size);
    s.checkedAt = s.writtenAt = GetTickCount64();
    return true;
}

// A slider writes on every movement: the file is written at most every 200 ms.
// Later changes wait for the next change, read or Flush().
inline bool FlushDue(State& s)
{
    if (s.pending.empty() || s.batch)
        return true;
    // A file being created is written at once, so a failure falls back to the
    // registry before anything else reads the settings.
    if (s.time && GetTickCount64() - s.writtenAt < 200)
        return true;
    return Flush(s);
}

inline LONG Change(const Op& op)
{
    auto& s = Shared();
    std::lock_guard guard(s.lock);
    if (!Refresh(s, true))
    {
        if (!RegistryChanges(op))
            return ERROR_SUCCESS;
        if (s.path.empty() || GetTickCount64() - s.failedAt < 10000)
            return RegistryApply(op);
        // The first change: the file starts from the registry settings.
        s.tree = RegistrySettings();
        s.ini = true;
        s.unreadable = false;
        s.time = s.size = 0;
        if (!Apply(s.tree, op))
            return ERROR_SUCCESS;
        s.pending.push_back(op);
        if (s.batch)
            return ERROR_SUCCESS;
        return Flush(s) ? ERROR_SUCCESS : ERROR_WRITE_FAULT;
    }
    if (!Apply(s.tree, op))
        return ERROR_SUCCESS;
    s.pending.push_back(op);
    return FlushDue(s) ? ERROR_SUCCESS : ERROR_WRITE_FAULT;
}

// Writes what is still waiting, for example when the settings window closes.
inline bool Flush()
{
    auto& s = Shared();
    std::lock_guard guard(s.lock);
    return Flush(s);
}

// Looks at the file now and returns Generation.
inline unsigned Sync()
{
    auto& s = Shared();
    std::lock_guard guard(s.lock);
    FlushDue(s);
    Refresh(s, true);
    return Generation.load();
}

// Groups the changes of one save into one write of the file.
class Batch
{
  public:
    Batch() : m_guard(Shared().lock)
    {
        ++Shared().batch;
    }

    ~Batch()
    {
        auto& s = Shared();
        if (!--s.batch)
            FlushDue(s);
    }

    Batch(const Batch&) = delete;
    Batch& operator=(const Batch&) = delete;

  private:
    std::lock_guard<std::recursive_mutex> m_guard;
};

inline struct FlushAtExit
{
    // Constructing the state first makes it outlive this object.
    FlushAtExit()
    {
        Shared();
    }

    ~FlushAtExit()
    {
        Flush();
    }
} g_flushAtExit;

// --- Access -------------------------------------------------------------------

inline bool Relative(const std::wstring& path, std::wstring& relative)
{
    const auto root = Root();
    if (path.size() < root.size() || _wcsnicmp(path.c_str(), root.c_str(), root.size()) ||
        (path.size() > root.size() && path[root.size()] != L'\\'))
        return false;
    relative = path.size() > root.size() ? path.substr(root.size() + 1) : L"";
    return true;
}

inline LONG CopyOut(const Value& value, DWORD* type, void* data, DWORD* bytes)
{
    if (type)
        *type = value.type;
    if (!bytes)
        return data ? ERROR_INVALID_PARAMETER : ERROR_SUCCESS;
    const DWORD needed = DWORD(value.data.size());
    if (data && *bytes < needed)
    {
        *bytes = needed;
        return ERROR_MORE_DATA;
    }
    if (data && needed)
        memcpy(data, value.data.data(), needed);
    *bytes = needed;
    return ERROR_SUCCESS;
}

// One settings key. Every call reads the current state, so a key opened
// before the file was created keeps working afterwards.
class Key
{
  public:
    Key() = default;

    explicit Key(std::wstring path) : m_path(std::move(path)) {}

    // Like RegQueryValueExW; data and bytes may be null.
    LONG Query(const wchar_t* name, DWORD* type, void* data, DWORD* bytes) const
    {
        auto& s = Shared();
        std::lock_guard guard(s.lock);
        FlushDue(s);
        if (Refresh(s))
        {
            const Node* node = Find(s.tree, m_path);
            if (!node)
                return ERROR_FILE_NOT_FOUND;
            const auto found = node->values.find(name ? name : L"");
            return found == node->values.end() ? ERROR_FILE_NOT_FOUND : CopyOut(found->second, type, data, bytes);
        }
        HKEY key;
        auto result = RegOpenKeyExW(HKEY_CURRENT_USER, Full(m_path).c_str(), 0, KEY_READ, &key);
        if (result != ERROR_SUCCESS)
            return result;
        result = RegQueryValueExW(key, name, nullptr, type, static_cast<BYTE*>(data), bytes);
        RegCloseKey(key);
        return result;
    }

    LONG Set(const wchar_t* name, DWORD type, const void* data, DWORD bytes) const
    {
        const auto first = static_cast<const BYTE*>(data);
        return Change({OpKind::Set, m_path, name ? name : L"", {type, std::vector<BYTE>(first, first + bytes)}});
    }

    LONG Delete(const wchar_t* name) const
    {
        return Change({OpKind::DeleteValue, m_path, name ? name : L"", {}});
    }

    // Names and values of the key, in name order.
    std::vector<std::pair<std::wstring, Value>> Values() const
    {
        std::vector<std::pair<std::wstring, Value>> list;
        auto& s = Shared();
        std::lock_guard guard(s.lock);
        FlushDue(s);
        if (Refresh(s))
        {
            if (const Node* node = Find(s.tree, m_path))
                list.assign(node->values.begin(), node->values.end());
            return list;
        }
        Node node;
        HKEY key;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, Full(m_path).c_str(), 0, KEY_READ, &key) == ERROR_SUCCESS)
        {
            ReadRegistry(key, node, m_path.empty());
            RegCloseKey(key);
        }
        list.assign(node.values.begin(), node.values.end());
        return list;
    }

    std::vector<std::wstring> SubKeys() const
    {
        std::vector<std::wstring> list;
        auto& s = Shared();
        std::lock_guard guard(s.lock);
        FlushDue(s);
        if (Refresh(s))
        {
            if (const Node* node = Find(s.tree, m_path))
                for (const auto& entry : node->keys)
                    list.push_back(entry.first);
            return list;
        }
        HKEY key;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, Full(m_path).c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS)
            return list;
        for (DWORD i = 0;; ++i)
        {
            wchar_t name[256];
            DWORD length = DWORD(std::size(name));
            if (RegEnumKeyExW(key, i, name, &length, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
                break;
            list.push_back(name);
        }
        RegCloseKey(key);
        return list;
    }

  private:
    std::wstring m_path; // below Root()
};

// The key at a path below Root(); values can be set before it exists.
inline Key At(const std::wstring& path)
{
    std::wstring relative;
    Relative(path, relative);
    return Key(relative);
}

inline bool Exists(const std::wstring& path)
{
    std::wstring relative;
    if (!Relative(path, relative))
        return false;
    auto& s = Shared();
    std::lock_guard guard(s.lock);
    FlushDue(s);
    if (Refresh(s))
        return Find(s.tree, relative) != nullptr;
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS)
        return false;
    RegCloseKey(key);
    return true;
}

// Like RegOpenKeyExW for reading or writing an existing key.
inline LONG Open(const std::wstring& path, Key& key)
{
    std::wstring relative;
    if (!Relative(path, relative))
        return ERROR_INVALID_PARAMETER;
    if (!Exists(path))
        return ERROR_FILE_NOT_FOUND;
    key = Key(relative);
    return ERROR_SUCCESS;
}

// Like RegCreateKeyExW.
inline LONG Create(const std::wstring& path, Key& key)
{
    std::wstring relative;
    if (!Relative(path, relative))
        return ERROR_INVALID_PARAMETER;
    const auto result = Change({OpKind::CreateKey, relative, L"", {}});
    if (result == ERROR_SUCCESS)
        key = Key(relative);
    return result;
}

// Like RegDeleteTreeW followed by deleting the key itself.
inline LONG DeleteTree(const std::wstring& path)
{
    std::wstring relative;
    if (!Relative(path, relative))
        return ERROR_INVALID_PARAMETER;
    return Change({OpKind::DeleteTree, relative, L"", {}});
}

// Like RegGetValueW: type REG_NONE accepts any type; strings end with a NUL.
inline LONG Get(const std::wstring& path, const wchar_t* name, DWORD type, void* data, DWORD* bytes)
{
    std::wstring relative;
    if (!Relative(path, relative))
        return ERROR_INVALID_PARAMETER;
    DWORD found = 0;
    const auto result = Key(relative).Query(name, &found, data, bytes);
    if (result != ERROR_SUCCESS && result != ERROR_MORE_DATA)
        return result;
    if (type != REG_NONE && found != type)
        return ERROR_UNSUPPORTED_TYPE;
    if (result == ERROR_SUCCESS && found == REG_SZ && data && bytes &&
        (*bytes < sizeof(wchar_t) || static_cast<const wchar_t*>(data)[*bytes / sizeof(wchar_t) - 1]))
        return ERROR_INVALID_DATA;
    return result;
}

// Like RegSetKeyValueW: creates the key when needed.
inline LONG Put(const std::wstring& path, const wchar_t* name, DWORD type, const void* data, DWORD bytes)
{
    std::wstring relative;
    if (!Relative(path, relative))
        return ERROR_INVALID_PARAMETER;
    return Key(relative).Set(name, type, data, bytes);
}

// A copy of a key with everything below it, e.g. to put it back later.
inline Node Subtree(const std::wstring& path)
{
    std::wstring relative;
    if (!Relative(path, relative))
        return {};
    auto& s = Shared();
    std::lock_guard guard(s.lock);
    FlushDue(s);
    if (Refresh(s))
    {
        const Node* node = Find(s.tree, relative);
        return node ? *node : Node{};
    }
    Node node;
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, KEY_READ, &key) == ERROR_SUCCESS)
    {
        ReadRegistry(key, node, relative.empty());
        RegCloseKey(key);
    }
    return node;
}

inline void AddOps(const Node& node, const std::wstring& path, std::vector<Op>& ops)
{
    ops.push_back({OpKind::CreateKey, path, L"", {}});
    for (const auto& [name, value] : node.values)
        ops.push_back({OpKind::Set, path, name, value});
    for (const auto& [name, child] : node.keys)
        AddOps(child, path.empty() ? name : path + L"\\" + name, ops);
}

// Makes a key exactly the given copy (an empty copy removes it).
inline LONG ReplaceSubtree(const std::wstring& path, const Node& node)
{
    std::wstring relative;
    if (!Relative(path, relative) || relative.empty())
        return ERROR_INVALID_PARAMETER;
    Batch batch;
    LONG result = Change({OpKind::DeleteTree, relative, L"", {}});
    if (node.Empty())
        return result;
    std::vector<Op> ops;
    AddOps(node, relative, ops);
    for (const auto& op : ops)
        if (result == ERROR_SUCCESS)
            result = Change(op);
    return result;
}

// Every setting in the layout of the settings file, for an export.
inline std::string Text()
{
    auto& s = Shared();
    std::lock_guard guard(s.lock);
    FlushDue(s);
    return Serialize(Refresh(s, true) ? s.tree : RegistrySettings());
}

// Whether the text is a whole settings file (an export of this layout).
inline bool IsSettingsText(const std::string& text)
{
    Node node;
    return text.find("[FrankyRenderer]") != text.npos && text.find("Storage=") != text.npos && Parse(text, node);
}

// Replaces every setting with those of a settings file text. The crash notes
// in the registry stay.
inline bool ReplaceAll(const std::string& text)
{
    Node node;
    if (!IsSettingsText(text) || !Parse(text, node))
        return false;
    auto& s = Shared();
    std::lock_guard guard(s.lock);
    Refresh(s, true);
    Batch batch;
    // Everything at the root that is not in the new text goes; then the text.
    const Node current = s.ini ? s.tree : RegistrySettings();
    for (const auto& [name, child] : current.keys)
        if (Change({OpKind::DeleteTree, name, L"", {}}) != ERROR_SUCCESS)
            return false;
    for (const auto& [name, value] : current.values)
        if (Change({OpKind::DeleteValue, L"", name, {}}) != ERROR_SUCCESS)
            return false;
    std::vector<Op> ops;
    AddOps(node, L"", ops);
    for (const auto& op : ops)
        if (Change(op) != ERROR_SUCCESS)
            return false;
    ++Generation;
    return true;
}
} // namespace SettingsStore
