import re,sys,os
wt,lx=sys.argv[1],sys.argv[2]
rmap={}
for l in open(wt+'/plan/ws244/phase003/rename-map.txt'):
    p=l.split()
    if len(p)==2: rmap[p[1]]=p[0]
def ldefs(path,cache={}):
    if path in cache: return cache[path]
    d={};txt=open(os.path.join(lx,path)).read().replace('\\\n',' ')
    for m in re.finditer(r'^\s*#\s*define\s+(\w+)(\([^)]*\))?\s*(.*)$',txt,re.M):
        d[m.group(1)]=(m.group(2) or '')+re.sub(r'/\*.*?\*/','',m.group(3))
    cache[path]=(d,txt);return cache[path]
def norm(s):
    for a,b in [(r'\bI915_INC_BIT64\b','BIT_ULL'),(r'\bI915_INC_GENMASK64\b','GENMASK_ULL'),(r'\bI915_INC_GENMASK\b','REG_GENMASK'),(r'\bGENMASK\b','REG_GENMASK'),(r'\bI915_INC_BIT\b','REG_BIT'),(r'\bBIT\b','REG_BIT'),(r'\bI915_INC_PAGE_SIZE\b','PAGE_SIZE'),(r'sizeof\(u32\)','4'),(r',\s*##\s*__VA_ARGS__',''),(r'\(MACRO__,\s*\.\.\.\)',''),(r'^\(MACRO__\)',''),(r'_MMIO\b',''),(r'\(x\)',''),(r'\bREG_FIELD_PREP\b','I915_INC_FIELD_PREP')]:
        s=re.sub(a,b,s)
    s=re.sub(r'/\*.*?\*/','',s);s=re.sub(r'//.*','',s)
    s=re.sub(r'\b(\w+)\b',lambda m:rmap.get(m.group(1),m.group(1)),s)
    s=re.sub(r'\b0x0*([0-9a-fA-F]+)\b',lambda m:str(int(m.group(1),16)),s)
    s=re.sub(r'\b([0-9]+)[uUlL]+\b',r'\1',s)
    return re.sub(r'[\s()]','',s)
ok=bad=miss=0
for f in sys.argv[3:]:
    txt=open(f).read().replace('\\\n',' ')
    for m in re.finditer(r'/\*((?:(?!\*/).)*?Values referenced: Linux v6\.19 ([\w/.\-]+)((?:(?!\*/).)*?))\*/(.*?)(?=\n\s*\n|/\*)',txt,re.S):
        path=m.group(2);names=re.findall(r'\w+',m.group(3))
        d,ltxt=ldefs(path)
        for n in names:
            if not re.search(r'\b'+n+r'\b',ltxt): print('MISSING-NAME',f,path,n);miss+=1
        for dm in re.finditer(r'#\s*define\s+(\w+)(\([^)]*\))?[ \t]*([^\n]*)',m.group(4)):
            ln=rmap.get(dm.group(1))
            if not ln or ln not in d: continue
            if norm((dm.group(2) or '')+dm.group(3))==norm(d[ln]): ok+=1
            else: bad+=1;print('DIFF',f,ln,'|',dm.group(3).strip(),'|',d[ln].strip())
print('ok',ok,'diff',bad,'missing-name',miss)
