"""Assemble a local FrankyRenderer package in dist; does not register it."""
import hashlib,json,pathlib,shutil
import pefile
ROOT=pathlib.Path(__file__).resolve().parent
OUT=ROOT.parent/'dist'
SOURCE=ROOT/'build/bin'
def main():
    OUT.mkdir(exist_ok=True)
    available={p.name.lower():p for p in SOURCE.iterdir() if p.suffix.lower() in ('.dll','.ax')}
    pending=['placeborenderer.ax'];seen=set()
    while pending:
        name=pending.pop()
        if name in seen:continue
        seen.add(name);path=available[name];pe=pefile.PE(str(path),fast_load=True)
        pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_IMPORT'],pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT']])
        for table in ('DIRECTORY_ENTRY_IMPORT','DIRECTORY_ENTRY_DELAY_IMPORT'):
            for entry in getattr(pe,table,[]):
                dependency=entry.dll.decode().lower()
                if dependency in available:pending.append(dependency)
        pe.close();shutil.copy2(path,OUT/path.name)
    for name in ('setup.ps1','Installieren.cmd','Entfernen.cmd','Installieren.vbs','Entfernen.vbs'):shutil.copy2(ROOT/name,OUT/name)
    shutil.copytree(ROOT/'shaders',OUT/'shaders',dirs_exist_ok=True)
    licenses=OUT/'licenses';licenses.mkdir(exist_ok=True)
    shutil.copy2(ROOT.parent/'LICENSE',licenses/'FrankyRenderer-GPLv3.txt')
    shutil.copy2(ROOT.parent/'THIRD_PARTY.md',licenses/'THIRD_PARTY.md')
    shutil.copytree(ROOT/'deps/msys/ucrt64/share/licenses',licenses/'dependencies',dirs_exist_ok=True)
    shutil.copytree(ROOT/'shaders',licenses/'shader-sources',dirs_exist_ok=True)
    shutil.copy2(ROOT/'deps/packages.lock.json',licenses/'packages.lock.json')
    shutil.copy2(ROOT.parent/'README.md',OUT/'README.md')
    shutil.copy2(ROOT.parent/'LICENSE',OUT/'LICENSE')
    shutil.copy2(ROOT.parent/'THIRD_PARTY.md',OUT/'THIRD_PARTY.md')
    manifest={str(p.relative_to(OUT)).replace('\\','/'):hashlib.sha256(p.read_bytes()).hexdigest() for p in OUT.rglob('*') if p.is_file() and 'registration-backup' not in p.parts and p.name!='build-manifest.json'}
    (OUT/'build-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print('Prepared',OUT)
if __name__=='__main__':main()
