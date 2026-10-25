"""Restore hash-verified MSYS2 packages from the checked-in lock file.

Python 3.14+ supplies tar.zst support. No global MSYS2 installation is needed.
"""
import hashlib,json,pathlib,tarfile,urllib.request
ROOT=pathlib.Path(__file__).resolve().parent
def main():
    for item in json.loads((ROOT/'deps/packages.lock.json').read_text()):
        url=item['url']
        if not url.startswith('https://mirror.msys2.org/mingw/ucrt64/'):
            raise ValueError('Unexpected package origin')
        cache=ROOT/'deps/packages'/url.rsplit('/',1)[1]
        cache.parent.mkdir(parents=True,exist_ok=True)
        if not cache.exists():
            with urllib.request.urlopen(url,timeout=120) as r:cache.write_bytes(r.read())
        if hashlib.sha256(cache.read_bytes()).hexdigest()!=item['sha256']:
            raise ValueError('SHA256 mismatch: '+cache.name)
        destination=ROOT/'deps/msys';destination.mkdir(parents=True,exist_ok=True)
        with tarfile.open(cache,'r:zst') as archive:archive.extractall(destination,filter='data')
        print('Verified',item['package'],flush=True)
    from build_libplacebo import main as build_libplacebo
    build_libplacebo()
if __name__=='__main__':main()
