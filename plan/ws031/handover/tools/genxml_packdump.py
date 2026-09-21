import sys, re
src = open(sys.argv[1]).read().split(chr(10))
for name in sys.argv[2:]:
    print('=== ' + name)
    on = False
    for line in src:
        if not on and line.startswith('GFX12_' + name + '_pack('):
            on = True
            continue
        if on:
            if line.startswith('}'):
                break
            m = re.search(r'(dw\[\d+\]|const uint\d+_t v\d+(_\d+)?) =', line)
            if m:
                print('  ' + m.group(1))
            m = re.search(r'util_bitpack_\w+\(values->([\w\[\]\.]+),\s*(\d+),\s*(\d+)', line)
            if m:
                print('      %-52s %s..%s' % (m.group(1), m.group(2), m.group(3)))
            m = re.search(r'__gen_(address|offset)\w*\(.*values->([\w\[\]\.]+)', line)
            if m:
                print('      %-52s (%s)' % (m.group(2), m.group(1)))
            m = re.search(r'GFX12_(\w+)_pack\(data, &dw\[(\d+)\], &values->(\w+)', line)
            if m:
                print('      -> %s at dw[%s] (%s)' % (m.group(1), m.group(2), m.group(3)))
