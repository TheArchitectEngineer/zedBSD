import json,subprocess,sys,os,concurrent.futures as cf
root='/home/awe/zedBSD-worktrees/b1'
base=open(root+'/plan/ws074/phase226/evidence/20261014-q1199/summary-after.txt').read().split('\n')
ids=[l.split()[0] for l in base if l.strip()]
out=sys.argv[1]; os.makedirs(out,exist_ok=True)
def run(i):
    f=out+'/'+str(abs(hash(i)))+'.json'
    subprocess.run(['python3',root+'/plan/ws074/tests/run-interop2025-harness.py','--manifest',root+'/plan/ws074/phase173/evidence/20261013-q1012/manifest.json','--test-id',i,'--out',f,'--font-dir',root+'/userland/desktop/fonts'],capture_output=True,text=True,cwd=root)
    try:
        d=json.load(open(f)); cb=d['result_callbacks']
        if isinstance(cb,list):
            tot=len(cb); ok=sum(1 for c in cb if (c.get('status')==0 if isinstance(c,dict) else False))
        else: tot=0; ok=0
        return '%s %s %d %d'%(i,d['outcome'],ok,tot)
    except Exception as e:
        return '%s error 0 0'%i
with cf.ThreadPoolExecutor(8) as ex: res=list(ex.map(run,ids))
open(out+'/summary.txt','w').write('\n'.join(res)+'\n')
