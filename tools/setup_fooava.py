#!/usr/bin/env python3
import os, shutil, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import extract_fooava
import recover_fooava_images

USAGE = """\
usage: setup_fooava.py <fooava installer .exe | unpacked folder> [skin folder]

Builds a ready-to-use fooAvA skin folder for Panels UI (reborn) in one step:
unpacks the installer (fooava_1_05_by_dawxxx666_*.exe, not run), copies its
images, converts the main configuration into fooava.txt + panels/ +
foo_ui_panels.ini, and each popup (*.ava: settings, playlist, about) into
panels/<name>.txt where the skin's buttons look for it.

The skin folder defaults to fooava in your Documents folder. It must not hold a
skin already (pass another folder, or delete it first).
Then point Panels UI at it: Preferences > Display > Panels UI (reborn) >
Browse... (or the layout wizard's "Use my own skin folder...").
"""

def documents_dir():
    home = os.path.expanduser('~')
    if os.name == 'nt':
        profile = os.environ.get('USERPROFILE', home)
        return os.path.join(profile, 'Documents')
    return os.path.join(home, 'Documents')

def config_blobs(root):
    found = []
    for dirpath, _, files in os.walk(root):
        for f in files:
            path = os.path.join(dirpath, f)
            if f.lower().endswith(('.png', '.jpg', '.jpeg', '.bmp', '.ico', '.dll', '.exe', '.ttf')):
                continue
            try:
                data = open(path, 'rb').read()
            except OSError:
                continue
            if b'$panel(' in data or f.lower().endswith('.ava'):
                found.append((path, data))
    return found

def images_dir(root):
    for dirpath, dirs, _ in os.walk(root):
        for d in dirs:
            if d.lower() == 'images' and any(s.lower() == 'fooava' for s in os.listdir(os.path.join(dirpath, d))):
                return os.path.join(dirpath, d)
    return None

def main(argv):
    if len(argv) < 2 or argv[1] in ('-h', '--help'):
        print(USAGE, end='')
        return 0 if len(argv) >= 2 else 2
    src = argv[1]
    dest = os.path.abspath(argv[2] if len(argv) > 2 else os.path.join(documents_dir(), 'fooava'))
    if os.path.exists(os.path.join(dest, 'fooava.txt')) or os.path.exists(os.path.join(dest, 'foo_ui_panels.ini')):
        print(f"{dest} already holds a skin: pass another folder, or delete it first")
        return 1

    tmp = None
    try:
        if os.path.isfile(src):
            tmp = tempfile.mkdtemp(prefix='fooava-')
            if recover_fooava_images.main(src, tmp, True) != 0:
                print(f"{src}: not a fooAvA installer this script can unpack")
                return 1
            root = tmp
        elif os.path.isdir(src):
            root = src
        else:
            print(f"{src}: no such file or folder")
            return 1

        images = images_dir(root)
        blobs = config_blobs(root)
        mains = sorted((b for b in blobs if b'///END' in b[1]), key=lambda b: len(b[1]), reverse=True)
        if not images or not mains:
            print(f"{src}: no fooAvA images/ folder or main configuration found in it")
            return 1
        main_cfg = mains[0][0]
        popups = [p for p, d in blobs if p != main_cfg and p.lower().endswith('.ava') and b'///END' not in d]

        os.makedirs(dest, exist_ok=True)
        shutil.copytree(images, os.path.join(dest, 'images'), dirs_exist_ok=True)
        print(f"images -> {os.path.join(dest, 'images')}")
        extract_fooava.main(main_cfg, os.path.join(dest, 'fooava.txt'))
        os.makedirs(os.path.join(dest, 'panels'), exist_ok=True)
        for p in sorted(popups):
            extract_fooava.main(p, os.path.join(dest, 'panels', os.path.basename(p) + '.txt'))
        if not popups:
            print("no popup configs (*.ava) found: the skin's settings/playlist/about buttons will say their script is missing")

        print(f"\nfooAvA skin ready in {dest}")
        print("Point Panels UI at it: Preferences > Display > Panels UI (reborn) > Browse...")
        return 0
    finally:
        if tmp:
            shutil.rmtree(tmp, ignore_errors=True)

if __name__ == '__main__':
    sys.exit(main(sys.argv))
