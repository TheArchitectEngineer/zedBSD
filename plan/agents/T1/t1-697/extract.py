import re,sys
r=sys.argv[1]
marks={}
for l in open(f'marks-{r}.txt'):
    m=re.match(r'MARK (\S+) session=(\d+) kernel=(\d+)',l)
    if m: marks[m[1]]=(int(m[2]),int(m[3]))
S=open(f'session-{r}.log').read().split('\n'); K=open(f'kernel-{r}.log').read().split('\n')
for sc,(b,e) in {'static':('static-begin','static-end'),'files':('files-begin','files-end'),'term+files':('term-begin','term-end')}.items():
    print('##',r,sc)
    for l in S[marks[b][0]:marks[e][0]]:
        if re.match(r'KWL PERF (compose|rec) ',l): print(l[:170])
    print('-- kernel')
    for l in K[marks[b][1]:marks[e][1]]:
        if 'perf' in l and 'runs 0 avg' not in l and not re.search(r'frames 0 wait|gpu 0 avg|draws 0 quads|streams 0 0KiB',l): print(l)
