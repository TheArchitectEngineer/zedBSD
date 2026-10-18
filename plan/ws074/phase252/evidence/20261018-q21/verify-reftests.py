from pathlib import Path
import json,gzip,tarfile,hashlib,shutil,subprocess,collections
from PIL import Image
q=Path('build/ws074-20261018/q21/verified');out=Path('plan/ws074/phase252/evidence/20261018-q21');hashof=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
d=json.loads((q/'scripted-413/report.json').read_text());old=json.loads(Path('build/ws074-20261018/q20/scripted-413/report.json').read_text());raw=Path(d['raw_directory']);a={x['test_id']:x for x in old['results']};b={x['test_id']:x for x in d['results']};assert a.keys()==b.keys();lost=[k for k in a if a[k]['outcome']=='pass' and b[k]['outcome']!='pass'];assert not lost,lost;assert d['completed']==d['selected']==413 and not d['unexecuted'] and not d['source_changed_during_run'];assert d['runner_sha256']==hashof('plan/ws074/tests/run-interop2025-scripted-reftests.py')
proof=json.loads((q/'source-proof.json').read_text());assert all(hashof(p)==v for p,v in proof.items());assert d['engine_library_sha256']==proof['build/ws074-20261018/q21/final/engine/libbrowser.so']
headers={};index=[];sources={};oldraw=Path(old['raw_directory']);oldcaps={c['resource']:json.loads((oldraw/c['record']).read_text()) for c in old['captures']};rgb_changed=[];rgb_unchanged=0
for c in d['captures']:
 r=json.loads((raw/c['record']).read_text());assert r['outcome']==c['outcome']
 if r['image']:
  assert hashof(r['image']['file'])==r['image']['sha256'];assert hashof(r['png']['file'])==r['png']['sha256']
  with Image.open(r['image']['file']) as image:pixels=image.convert('RGB').tobytes()
  with Image.open(r['png']['file']) as image:assert image.size==(800,600) and image.convert('RGB').tobytes()==pixels
  assert hashlib.sha256(pixels).hexdigest()==r['png']['rgb_sha256']
 if r['outcome']=='pass':assert r['capture_boundary']['valid'] and not r['unsupported'] and r['readiness']['status']=='ready'
 for ack in r['response_headers']:
  if ack['sources']:assert ack['applied'] and ack['error'] is None
  for src in ack['sources']:
   if src['path'] not in sources:sources[src['path']]=hashlib.sha256(subprocess.check_output(['git','-C','build/ws074-suites/wpt','show',d['wpt_commit']+':'+src['path']])).hexdigest()
   assert sources[src['path']]==src['sha256'];headers[src['path']]=src['sha256']
 o=oldcaps.get(c['resource'])
 if o and o.get('png') and r.get('png'):
  if o['png']['rgb_sha256']==r['png']['rgb_sha256']:rgb_unchanged+=1
  else:rgb_changed.append(c['resource'])
 index.append(dict(resource=c['resource'],record=c['record'],outcome=c['outcome'],image=r.get('image'),png=r.get('png'),boundary=r['capture_boundary']))
trans=collections.Counter((a[k]['outcome'],b[k]['outcome']) for k in a);comparison=dict(before=old['counts'],after=d['counts'],whole_regressions=lost,gains=[k for k in a if a[k]['outcome']!='pass' and b[k]['outcome']=='pass'],transitions=[dict(before=x,after=y,count=n) for (x,y),n in trans.items()],common_rgb_unchanged=rgb_unchanged,common_rgb_changed=rgb_changed,header_sources=headers);(q/'scripted-comparison.json').write_text(json.dumps(comparison,indent=2)+'\n');shutil.copy2(q/'scripted-comparison.json',out/'scripted-comparison.json')
for name,src in [('scripted-report.json.gz',q/'scripted-413/report.json'),('scripted-progress.jsonl.gz',Path(d['checkpoint']))]:(out/name).write_bytes(gzip.compress(src.read_bytes(),mtime=0))
archive=out/'scripted-all-native-captures.tar.gz'
with tarfile.open(archive,'w:gz') as tar:
 for p in sorted(raw.iterdir()):
  if p.is_file():tar.add(p,arcname=p.name,recursive=False)
with tarfile.open(archive,'r:gz') as tar:
 for p in sorted(raw.iterdir()):
  if p.is_file():assert tar.extractfile(p.name).read()==p.read_bytes()
(out/'scripted-capture-index.json.gz').write_bytes(gzip.compress((json.dumps(index,indent=2)+'\n').encode(),mtime=0))
crash=json.loads((q/'crash-report.json').read_text());before=json.loads(Path('build/ws074-20261018/q19/final/crash-report.json').read_text());ca={x['test_id']:x['outcome'] for x in before['results']};cb={x['test_id']:x['outcome'] for x in crash['results']};assert ca.keys()==cb.keys() and all(cb[k]=='pass' for k in ca if ca[k]=='pass');(out/'crash-comparison.json').write_text(json.dumps(dict(before=before['counts'],after=crash['counts'],changes={k:[ca[k],cb[k]] for k in ca if ca[k]!=cb[k]}),indent=2)+'\n')
cr=Path(crash['raw_directory']);files=[p for p in cr.iterdir() if p.is_file()];archive=out/'crash-all-raw.tar.gz'
with tarfile.open(archive,'w:gz') as tar:
 for p in sorted(files):tar.add(p,arcname=p.name,recursive=False)
with tarfile.open(archive,'r:gz') as tar:
 for p in sorted(files):assert tar.extractfile(p.name).read()==p.read_bytes()
(out/'crash-report.json.gz').write_bytes(gzip.compress((q/'crash-report.json').read_bytes(),mtime=0));print(comparison);print(crash['counts'])
