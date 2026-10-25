# Packaged runtime names

Release packages use explicit names for the runtime DLLs. The development SDK
and build output retain the upstream names. `src/package.py` applies the mapping
through `src/runtime_names.py` when assembling the installation package.

| Upstream filename | Packaged filename |
|---|---|
| libplacebo-387.dll | VideoProcessing.dll |
| libdovi.dll | DolbyVision.dll |
| liblcms2-2.dll | ColorManagement.dll |
| libshaderc_shared.dll | ShaderCompiler.dll |
| libspirv-cross-c-shared.dll | ShaderTranslator.dll |
| vulkan-1.dll | VulkanLoader.dll |
| libstdc++-6.dll | CppRuntime.dll |
| libgcc_s_seh-1.dll | CompilerRuntime.dll |
| libwinpthread-1.dll | Threading.dll |

The package transformation adds a read-only PE section containing the new names
and updates import, delay-import and export filename pointers. Executable
sections, exported functions, version information and license notices remain
unchanged. Original filenames in version information identify the upstream
libraries. The existing bound-import cache is discarded and the PE checksum is
recalculated.

Packaging rejects signed binaries, unsupported delay imports, insufficient
header space and filename literals outside the recognized PE and version tables.
This requires review if a future upstream library changes its loading behavior.
It also checks that every renamed import resolves within the package and that
no old runtime filenames remain in the installation output.

`licenses/runtime-names.json` records the original and packaged filenames and
SHA-256 hashes of every included binary, including the renderer. Dependency
versions, official sources and license notices remain documented separately in
`licenses/packages.lock.json`, `licenses/libplacebo-source.lock.json`,
`THIRD_PARTY.md` and the dependency license folder.
