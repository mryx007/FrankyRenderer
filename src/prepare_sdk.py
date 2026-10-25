import pathlib,shutil,sys
import pefile
root=pathlib.Path(__file__).resolve().parent
sdk=root/'deps'/'msys'/'ucrt64';out=root/'build'/'bin';out.mkdir(parents=True,exist_ok=True)
(root/'build/lib').mkdir(parents=True,exist_ok=True)
(root/'build/obj').mkdir(parents=True,exist_ok=True)
for source in (sdk/'bin').glob('*.dll'):shutil.copy2(source,out/source.name)
pe=pefile.PE(str(out/'libplacebo-387.dll'))
exports=[]
for item in pe.DIRECTORY_ENTRY_EXPORT.symbols:
    if not item.name:continue
    section=pe.get_section_by_rva(item.address)
    exports.append(item.name.decode()+(' DATA' if section and not section.Characteristics&0x20000000 else ''))
(root/'build'/'libplacebo.def').write_text('LIBRARY libplacebo-387.dll\nEXPORTS\n'+'\n'.join(exports)+'\n')
print('Prepared matching libplacebo 387 headers, DLLs and MSVC import definition')
