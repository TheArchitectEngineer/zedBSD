#!/usr/bin/env python3
# Moves Markdown records with git mv and rewrites the relative links that point at them, and the relative links
# inside them, so that every link keeps resolving to the same file.  Run from the repository root:
#   python3 -I plan/tools/record-audit/relink.py [--dry-run] OLD=NEW [OLD=NEW ...]
# OLD and NEW are paths from the repository root; a directory moves with everything under it.
# Links are [text](path) outside code spans and fences; URLs and bare anchors are left alone.  Only .md files
# tracked by git are rewritten.  The script prints each rewritten file and the number of links it changed.
import os
import re
import subprocess
import sys
import urllib.parse

LINK = re.compile(r'(\[[^\]]*\]\()([^)\s]+)((?:\s+"[^"]*")?\))')


def moved_path(path, moves):
    for old, new in moves:
        if path == old:
            return new
        if path.startswith(old + '/'):
            return new + path[len(old):]
    return path


def rewrite_line(line, old_dir, new_dir, moves, self_moved):
    count = 0
    pieces = re.split(r'(`[^`]*`)', line)
    for index, piece in enumerate(pieces):
        if piece.startswith('`'):
            continue

        def replace(match):
            nonlocal count
            target = match.group(2)
            if re.match(r'^[a-z][a-z0-9+.-]*:', target, re.I) or target.startswith('#'):
                return match.group(0)
            path_part, hash_mark, anchor = target.partition('#')
            if path_part == '':
                return match.group(0)
            resolved = os.path.normpath(os.path.join(old_dir, urllib.parse.unquote(path_part)))
            destination = moved_path(resolved, moves)
            if destination == resolved and not self_moved:
                return match.group(0)
            relative = os.path.relpath(destination, new_dir)
            if path_part.endswith('/') and not relative.endswith('/'):
                relative += '/'
            if relative == path_part:
                return match.group(0)
            count += 1
            return match.group(1) + relative + hash_mark + anchor + match.group(3)

        pieces[index] = LINK.sub(replace, piece)
    return ''.join(pieces), count


def main(argv):
    dry_run = False
    moves = []
    for argument in argv:
        if argument == '--dry-run':
            dry_run = True
            continue
        old, new = argument.split('=', 1)
        moves.append((os.path.normpath(old), os.path.normpath(new)))
    files = subprocess.run(['git', 'ls-files', '*.md'], capture_output=True, text=True, check=True).stdout.split()
    for path in files:
        new_path = moved_path(path, moves)
        old_dir = os.path.dirname(path)
        new_dir = os.path.dirname(new_path)
        self_moved = new_path != path
        with open(path, encoding='utf-8') as stream:
            lines = stream.read().split('\n')
        changed = 0
        in_fence = False
        for index, line in enumerate(lines):
            if line.lstrip().startswith('```'):
                in_fence = not in_fence
                continue
            if in_fence:
                continue
            lines[index], count = rewrite_line(line, old_dir, new_dir, moves, self_moved)
            changed += count
        if changed:
            print(f'{changed:4d} {path}')
            if not dry_run:
                with open(path, 'w', encoding='utf-8') as stream:
                    stream.write('\n'.join(lines))
    if not dry_run:
        for old, new in moves:
            os.makedirs(os.path.dirname(new), exist_ok=True)
            subprocess.run(['git', 'mv', old, new], check=True)
    return 0


sys.exit(main(sys.argv[1:]))
