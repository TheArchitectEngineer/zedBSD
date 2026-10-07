# Q1 の merge の道具（2026-10-07 に /tmp から写した）: merge_one SHA。
merge_one () 
{ 
    git merge --no-ff --no-commit $1 > /tmp/claude-1000/m.txt 2>&1;
    u=$(git diff --name-only --diff-filter=U);
    if [ -n "$u" ]; then
        if [ "$u" = plan/agents/T1/requests.md ]; then
            python3 - <<'EOF'
p='plan/agents/T1/requests.md'
L=open(p).read().split('\n')
while '<<<<<<< HEAD' in L:
    a=L.index('<<<<<<< HEAD'); m=L.index('=======',a); b=next(i for i in range(m,len(L)) if L[i].startswith('>>>>>>> '))
    head=L[a+1:m]; theirs=L[m+1:b]
    ids=[x.split('|')[1].strip() for x in theirs if x.startswith('|')]
    L=L[:a]+theirs+[x for x in head if not x.startswith('|') or x.split('|')[1].strip() not in ids]+L[b+1:]
open(p,'w').write('\n'.join(L))
EOF

            git add plan/agents/T1/requests.md;
        else
            echo "CONFLICT $1: $u"
            return 1;
        fi;
    fi;
    if git grep -q '^<<<<<<< ' -- plan userland; then
        echo MARKERS;
        return 1;
    fi;
    echo "$1 $(git diff --cached --stat | tail -1)";
    git commit -q -m WIP
}
