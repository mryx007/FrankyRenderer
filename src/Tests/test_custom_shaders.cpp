#include "Settings/CustomShaders.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>

static void Require(bool ok, const char* message)
{
    if (ok)
        return;
    printf("FAIL %s\n", message);
    exit(1);
}

static void WriteFile(const std::filesystem::path& path, const char* text)
{
    std::ofstream out(path, std::ios::binary);
    out << text;
}

static int SlotOf(const char* file)
{
    for (int id = ShaderBuiltinCount; id < ShaderCount; ++id)
        if (!strcmp(ShaderCatalog[id].file, file))
            return id;
    return -1;
}

static constexpr char Sharp[] = "//!PARAM strength\n"
                                "//!DESC Strength\n"
                                "//!TYPE float\n"
                                "//!MINIMUM 0.25\n"
                                "//!MAXIMUM 2.0\n"
                                "0.5\n"
                                "\n"
                                "//!PARAM taps\n"
                                "//!TYPE DYNAMIC int\n"
                                "3\n"
                                "\n"
                                "//!PARAM mode\n"
                                "//!TYPE ENUM int\n"
                                "SOFT\n"
                                "HARD\n"
                                "\n"
                                "//!HOOK MAIN\n"
                                "//!BIND HOOKED\n"
                                "vec4 hook() { return HOOKED_tex(HOOKED_pos); }\n";

int main()
{
    wchar_t root[80];
    swprintf_s(root, L"Software\\PlaceboRendererTest_custom_%lu", GetCurrentProcessId());
    SetEnvironmentVariableW(L"PLACEBO_RENDERER_TEST_ROOT", root);
    const auto folder = CustomShaders::Folder();
    std::error_code error;
    std::filesystem::remove_all(folder, error);
    std::filesystem::create_directories(folder);

    CustomShaders::Refresh();
    Require(ShaderListedCount() == ShaderBuiltinCount, "an empty folder adds nothing");
    Require(!strcmp(ShaderCatalog[0].file, BuiltinShaderCatalog[0].file), "built-in shaders keep their numbers");

    WriteFile(folder / L"sharp.glsl", Sharp);
    WriteFile(folder / L"second.hook", "//!HOOK MAIN\nvec4 hook() { return vec4(0.0); }\n");
    WriteFile(folder / L"mpc.glsl", "float4 main(float2 tex : TEXCOORD0) : COLOR { return 0; }\n");
    WriteFile(folder / L"notes.txt", "//!HOOK MAIN\n");
    CustomShaders::Refresh();
    Require(ShaderListedCount() == ShaderBuiltinCount + 3, "only .glsl and .hook files are listed");

    const int sharp = SlotOf("sharp.glsl"), second = SlotOf("second.hook"), mpc = SlotOf("mpc.glsl");
    Require(sharp >= ShaderBuiltinCount && second >= 0 && mpc >= 0, "files get custom slots");
    const auto& info = ShaderCatalog[sharp];
    Require(info.usable && !wcscmp(info.label, L"sharp"), "shader name comes from the file");
    Require(info.count == 3, "parameters come from the file");
    Require(!strcmp(info.parameters[0].name, "strength") && !info.parameters[0].integer &&
                info.parameters[0].initial == .5f && info.parameters[0].minimum == .25f &&
                info.parameters[0].maximum == 2.f,
            "float parameter");
    Require(info.parameters[1].integer && info.parameters[1].initial == 3 && info.parameters[1].maximum >= 3,
            "integer parameter gets limits without any given");
    Require(info.choices && info.choices[2].size() == 2 && info.choices[2][1] == L"HARD" &&
                info.parameters[2].maximum == 1,
            "enum parameter lists its names");
    Require(ShaderCatalog[second].usable && !ShaderCatalog[second].count, "shader without parameters is on/off only");
    Require(!ShaderCatalog[mpc].usable && wcsstr(ShaderCatalog[mpc].label, L"not compatible"),
            "a file without //!HOOK is marked");

    const auto list = ShaderListOrder();
    Require(int(list.size()) == ShaderListedCount(), "the list shows every shader once");
    for (size_t i = 1; i < list.size(); ++i)
        Require(_wcsicmp(ShaderCatalog[list[i - 1]].label, ShaderCatalog[list[i]].label) <= 0,
                "the list is alphabetical");

    UserShaderSettings settings;
    settings.order[0] = sharp;
    settings.order[sharp] = 0;
    MoveEmptyShaderSlotsLast(settings.order);
    int listed = 0;
    for (int position = 0; position < ShaderCount; ++position)
        if (ShaderListed(settings.order[position]))
            Require(position == listed++, "listed shaders come first");
    Require(settings.order[0] == sharp, "moving empty slots keeps the chain order");

    // Values outside the current limits must not throw away all settings.
    settings.shaders[sharp].enabled = 1;
    settings.shaders[sharp].overrides = 1;
    settings.shaders[sharp].values[0] = 100;
    Require(ValidUserShaders(settings), "a changed custom shader keeps the settings valid");

    std::filesystem::remove(folder / L"second.hook");
    WriteFile(folder / L"third.glsl", "//!HOOK MAIN\nvec4 hook() { return vec4(1.0); }\n");
    CustomShaders::Refresh();
    Require(SlotOf("sharp.glsl") == sharp, "a file keeps its slot");
    Require(SlotOf("second.hook") < 0, "a deleted file leaves the list");
    Require(SlotOf("third.glsl") >= 0 && SlotOf("third.glsl") != second, "a new file does not inherit a deleted slot");

    WriteFile(folder / L"second.hook", "//!HOOK MAIN\nvec4 hook() { return vec4(0.0); }\n");
    CustomShaders::Refresh();
    Require(SlotOf("second.hook") == second, "a file that comes back finds its settings again");

    for (int i = 0; i < ShaderCustomCount + 5; ++i)
    {
        wchar_t name[32];
        swprintf_s(name, L"many%02d.glsl", i);
        WriteFile(folder / name, "//!HOOK MAIN\nvec4 hook() { return vec4(0.0); }\n");
    }
    CustomShaders::Refresh();
    Require(ShaderListedCount() == ShaderCount, "the list stops at the number of slots");
    Require(SlotOf("sharp.glsl") == sharp, "many new files do not move existing ones");

    // A deleted built-in shader leaves the list and comes back with its file.
    {
        const auto builtin = folder.parent_path() / L"PlayKit";
        const auto file = builtin / BuiltinShaderCatalog[0].file;
        auto moved = file;
        moved += L".moved";
        Require(std::filesystem::exists(file), "built-in shader present for the test");
        std::filesystem::rename(file, moved);
        CustomShaders::Refresh();
        const bool listed = ShaderListed(0);
        const auto without = ShaderListOrder();
        UserShaderSettings kept;
        kept.shaders[0].enabled = 1;
        const bool valid = ValidUserShaders(kept);
        std::filesystem::rename(moved, file);
        Require(!listed && std::find(without.begin(), without.end(), 0) == without.end(),
                "a deleted built-in shader leaves the list");
        Require(valid, "settings of a deleted built-in shader stay valid");
        CustomShaders::Refresh();
        Require(ShaderListed(0), "a restored built-in shader returns");
    }

    // A package unpacked into its own folder, with mpv's input.conf.
    std::filesystem::remove_all(folder, error);
    std::filesystem::create_directories(folder / L"Pack" / L"shaders");
    std::filesystem::create_directories(folder / L"Pack" / L"__MACOSX");
    const auto hook = "//!HOOK MAIN\nvec4 hook() { return vec4(0.0); }\n";
    WriteFile(folder / L"Pack" / L"shaders" / L"a.glsl", hook);
    WriteFile(folder / L"Pack" / L"shaders" / L"b.glsl", hook);
    WriteFile(folder / L"Pack" / L"shaders" / L"unused.glsl", hook);
    WriteFile(folder / L"Pack" / L"__MACOSX" / L"junk.glsl", hook);
    WriteFile(folder / L"loose.glsl", hook);
    WriteFile(
        folder / L"Pack" / L"input.conf",
        "# comment\n"
        "CTRL+1 no-osd change-list glsl-shaders set \"~~/shaders/a.glsl;~~/shaders/b.glsl\"; show-text \"Mode A\"\n"
        "CTRL+2 no-osd change-list glsl-shaders set \"~~/shaders/b.glsl;~~/shaders/gone.glsl\"; show-text \"Mode B\"\n"
        "CTRL+0 no-osd change-list glsl-shaders clr \"\"; show-text \"cleared\"\n");
    CustomShaders::Refresh();
    Require(ShaderListedCount() == ShaderBuiltinCount + 3, "a package shows its chains, not its files");
    int modeA = -1, modeB = -1;
    for (int id = ShaderBuiltinCount; id < ShaderCount; ++id)
    {
        if (!wcscmp(ShaderCatalog[id].label, L"Mode A"))
            modeA = id;
        if (wcsstr(ShaderCatalog[id].label, L"Mode B"))
            modeB = id;
    }
    Require(modeA >= 0 && ShaderCatalog[modeA].usable, "chain takes its name from the package");
    Require(ShaderCatalog[modeA].parts->size() == 2 &&
                ShaderCatalog[modeA].parts->back().find(L"b.glsl") != std::wstring::npos,
            "chain keeps its files in order");
    Require(modeB >= 0 && !ShaderCatalog[modeB].usable, "a chain with a missing file cannot be switched on");
    Require(SlotOf("loose.glsl") >= 0, "a loose file beside a package stays listed");

    // The same package unpacked straight into the folder.
    std::filesystem::remove_all(folder, error);
    std::filesystem::create_directories(folder / L"shaders");
    WriteFile(folder / L"shaders" / L"a.glsl", hook);
    WriteFile(folder / L"mine.glsl", hook);
    WriteFile(folder / L"input.conf", "K glsl-shaders set \"~~/shaders/a.glsl\"; show-text \"Flat\"\n");
    CustomShaders::Refresh();
    Require(ShaderListedCount() == ShaderBuiltinCount + 2, "a package without its own folder works too");
    Require(SlotOf("mine.glsl") >= 0, "own files beside a flat package stay listed");

    std::filesystem::remove_all(folder, error);
    CustomShaders::Refresh();
    Require(ShaderListedCount() == ShaderBuiltinCount, "an empty folder removes the entries");
    RegDeleteTreeW(HKEY_CURRENT_USER, root);
    puts(
        "PASS custom shaders: folder scan, parameters, enums, not compatible files, stable slots, slot limit, packages");
    return 0;
}
