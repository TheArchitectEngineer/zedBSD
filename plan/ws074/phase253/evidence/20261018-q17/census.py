from pathlib import Path
import subprocess,time,json,sys,os
root=Path('build/ws074-20261018/q17/final');phase=sys.argv[1];program=sys.argv[2];outroot=root/(phase+'-harness');outroot.mkdir(exist_ok=True);start=time.monotonic();runs=[]
for offset in range(0,1491,128):
 out=outroot/('shard-%04d'%offset);command=['python3','plan/ws074/tests/run-interop2025-census.py','--manifest','plan/ws074/phase173/evidence/20261013-q1012/manifest.json','--program',program,'--out',str(out),'--offset',str(offset),'--limit','128','--settle-ms','5000','--process-timeout','20','--font-dir','userland/desktop/fonts','--gzip-raw'];environment=dict(os.environ,XDG_DATA_HOME=str((outroot/'data').resolve()))
 with (outroot/('shard-%04d.log'%offset)).open('w') as log:run=subprocess.run(command,stdout=log,stderr=log,env=environment,timeout=max(1,2400-(time.monotonic()-start)))
 assert run.returncode==0
 report=json.loads((out/'report.json').read_text());assert report['eligible']==1491
 row=dict(offset=offset,exit=run.returncode,seconds=round(time.monotonic()-start,1),counts=report['outcome_counts'],command=command);runs.append(row);(outroot/'runs.json').write_text(json.dumps(runs,indent=2)+'\n');print({k:v for k,v in row.items() if k!='command'},flush=True)
