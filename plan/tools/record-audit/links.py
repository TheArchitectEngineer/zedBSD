#!/usr/bin/env python3
# Reports broken relative links in the Markdown records.  A link is [text](path) whose path is not a URL or a bare
# anchor; it is resolved against the directory of the file that holds it, with any #anchor removed.
# Run from the repository root:
#   python3 -I plan/tools/record-audit/links.py           # the entry files (AGENTS.md, .claude/agents, plan/ top level, plan/agents)
#   python3 -I plan/tools/record-audit/links.py --all     # every .md under plan/ as well; prints a count per file
#   python3 -I plan/tools/record-audit/links.py FILE...   # only the named files
# Each broken link is printed as "BROKEN file:line target".  The exit status is 1 when a link is broken.
import glob
import os
import re
import sys
import urllib.parse

LINK = re.compile(r'\[[^\]]*\]\(([^)\s]+)(?:\s+"[^"]*")?\)')
CODE = re.compile(r'`[^`]*`')


def entry_files():
    files = ['AGENTS.md', 'README.md']
    files += sorted(glob.glob('.claude/agents/*.md'))
    files += sorted(glob.glob('plan/*.md'))
    files += sorted(glob.glob('plan/agents/*.md'))
    files += ['plan/history/index.md']
    return [f for f in files if os.path.isfile(f)]


def broken_links(path):
    found = []
    base = os.path.dirname(path)
    in_fence = False
    with open(path, encoding='utf-8', errors='replace') as stream:
        for number, line in enumerate(stream, 1):
            if line.lstrip().startswith('```'):
                in_fence = not in_fence
                continue
            if in_fence:
                continue
            text = CODE.sub('', line)
            for match in LINK.finditer(text):
                target = match.group(1)
                if re.match(r'^[a-z][a-z0-9+.-]*:', target, re.I) or target.startswith('#'):
                    continue
                target_path = urllib.parse.unquote(target.split('#', 1)[0])
                if target_path == '':
                    continue
                if not os.path.exists(os.path.normpath(os.path.join(base, target_path))):
                    found.append((number, target))
    return found


def main(argv):
    show_counts = False
    files = []
    for argument in argv:
        if argument == '--all':
            show_counts = True
        else:
            files.append(argument)
    if not files:
        files = entry_files()
        if show_counts:
            files += [f for f in sorted(glob.glob('plan/**/*.md', recursive=True)) if f not in files]
    total = 0
    for path in files:
        found = broken_links(path)
        total += len(found)
        if show_counts and found:
            print(f'COUNT {len(found)} {path}')
        if not show_counts or path in entry_files():
            for number, target in found:
                print(f'BROKEN {path}:{number} {target}')
    print(f'TOTAL {total} broken in {len(files)} files')
    status = 0
    if total:
        status = 1
    return status


sys.exit(main(sys.argv[1:]))
