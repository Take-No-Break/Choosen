"""Verify public release identity and archive contents, without executing either app.

This is not a source build, vulnerability scan, or malware verdict.
"""
import hashlib
import io
import json
from pathlib import Path
import re
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def verify(data, spec):
    if len(data) != spec['bytes']:
        raise ValueError('Size mismatch: ' + spec['name'])
    if hashlib.sha256(data).hexdigest() != spec['sha256']:
        raise ValueError('SHA256 mismatch: ' + spec['name'])
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        if archive.testzip() is not None:
            raise ValueError('Corrupt archive')
        names = set(archive.namelist())
        if spec['kind'] == 'apk':
            required = {'AndroidManifest.xml', 'classes.dex',
                        'lib/arm64-v8a/libchoosen_core.so',
                        'assets/v3/index.html', 'assets/v3/app.js', 'assets/v3/app.css'}
            if not required <= names:
                raise ValueError('Missing APK payload')
            if not archive.read('classes.dex').startswith(b'dex\n'):
                raise ValueError('Invalid DEX header')
            if not archive.read('lib/arm64-v8a/libchoosen_core.so').startswith(b'\x7fELF'):
                raise ValueError('Invalid native library')
        else:
            if names != {'Choosen.exe', 'Start-Choosen.cmd', 'README.md', 'THIRD-PARTY-NOTICES.txt'}:
                raise ValueError('Unexpected Windows payload')
            exe = archive.read('Choosen.exe')
            if not exe.startswith(b'MZ') or hashlib.sha256(exe).hexdigest() != spec['exe_sha256']:
                raise ValueError('Windows executable mismatch')


def main():
    manifest = json.loads((ROOT / 'checks/release.json').read_text(encoding='utf-8'))
    for spec in manifest['artifacts']:
        url = ('https://github.com/Take-No-Break/Choosen/releases/download/'
               + manifest['tag'] + '/' + spec['name'])
        request = urllib.request.Request(url, headers={'User-Agent': 'Choosen-release-checks'})
        with urllib.request.urlopen(request, timeout=90) as response:
            data = response.read(32 * 1024 * 1024)
        verify(data, spec)
        print('PASS published checksum and archive:', spec['name'])
    for page in [ROOT / 'README.md', *sorted((ROOT / 'docs').glob('*.md'))]:
        for target in re.findall(r'\]\(([^)]+)\)', page.read_text(encoding='utf-8')):
            if not target.startswith(('https://', 'http://', '#')):
                path = (page.parent / target.split('#')[0]).resolve()
                if not path.is_relative_to(ROOT) or not path.exists():
                    raise ValueError('Missing local documentation target: ' + target)
    print('PASS local documentation links')


if __name__ == '__main__':
    main()
