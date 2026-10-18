from pathlib import Path
import json,gzip,tarfile,hashlib,shutil,subprocess,collections
from PIL import Image
q=Path('build/ws074-20261018/q20');out=Path('plan/ws074/phase190/evidence/20261018-q20');out.mkdir(parents=True,exist_ok=True)
hashof=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
d=json.loads((q/'scripted-413/report.json').read_text());raw=Path(d['raw_directory']);assert d['completed']==d['selected']==d['captured_cases']==413 and not d['unexecuted'] and not d['source_changed_during_run']
proof=json.loads((q/'pair-proof.json').read_text());assert all(hashof(p)==v for p,v in proof.items());assert d['runner_sha256']==hashof('plan/ws074/tests/run-interop2025-scripted-reftests.py')
assert d['engine_library_sha256']==proof[str(q/'engine/libbrowser.so')];assert d['program_sha256']==proof[str(q/'engine/wpt-ready-render')]
header_sources={};index=[];old=json.loads(Path('build/ws074-20261018/q19/final/scripted-413/report.json').read_text());oldraw=Path(old['raw_directory']);oldcaptures={r['resource']:json.loads((oldraw/r['record']).read_text()) for r in old['captures']};preserved_pixels=0;changed_pixels=[]
for item in d['captures']:
 r=json.loads((raw/item['record']).read_text());assert r['outcome']==item['outcome']
 if r['image']:
  assert hashof(r['image']['file'])==r['image']['sha256'];assert hashof(r['png']['file'])==r['png']['sha256']
  with Image.open(r['image']['file']) as p:rgb=p.convert('RGB').tobytes()
  with Image.open(r['png']['file']) as p:assert p.size==(800,600) and p.convert('RGB').tobytes()==rgb
  assert hashlib.sha256(rgb).hexdigest()==r['png']['rgb_sha256']
 if r['outcome']=='pass':assert r['capture_boundary']['valid'] and not r['unsupported'] and r['readiness']['status']=='ready'
 for ack in r['response_headers']:
  if ack['sources']:assert ack['applied'] and ack['error'] is None
  for src in ack['sources']:
   blob=subprocess.check_output(['git','-C','build/ws074-suites/wpt','show',d['wpt_commit']+':'+src['path']]);assert hashlib.sha256(blob).hexdigest()==src['sha256'];header_sources[src['path']]=src['sha256']
 previous=oldcaptures.get(item['resource'])
 if previous and previous.get('png') and r.get('png'):
  if previous['png']['rgb_sha256']==r['png']['rgb_sha256']:preserved_pixels+=1
  else:changed_pixels.append(item['resource'])
 index.append(dict(resource=item['resource'],record=item['record'],outcome=item['outcome'],image=r.get('image'),png=r.get('png'),boundary=r['capture_boundary']))
assert not subprocess.check_output(['git','-C','build/ws074-suites/wpt','status','--porcelain','--untracked-files=no']).strip()
proof.update({'userland/desktop/libbrowser/page/page.c':hashof('userland/desktop/libbrowser/page/page.c')});assert proof['userland/desktop/libbrowser/page/page.c']==json.loads(Path('build/ws074-20261018/q19/final/source-proof.json').read_text())['userland/desktop/libbrowser/page/page.c']
(q/'source-proof.json').write_text(json.dumps(proof,indent=2)+'\n')
(q/'header-and-pixel-readback.json').write_text(json.dumps(dict(header_sources=header_sources,common_captures_rgb_unchanged=preserved_pixels,common_captures_rgb_changed=changed_pixels,all_images_hash_rgb_readback=True,wpt_tracked_clean=True),indent=2)+'\n')
for name,src in [('report.json.gz',q/'scripted-413/report.json'),('progress.jsonl.gz',Path(d['checkpoint']))]:(out/name).write_bytes(gzip.compress(src.read_bytes(),mtime=0))
for p in [q/'pinned-pixel-verification.json',q/'controls.log',q/'source-proof.json',q/'comparison.json',q/'header-and-pixel-readback.json',q/'verify-pinned-pixels.py',Path(__file__)]:shutil.copy2(p,out/p.name)
archive=out/'all-native-captures.tar.gz'
with tarfile.open(archive,'w:gz') as tar:
 for p in sorted(raw.iterdir()):
  if p.is_file():tar.add(p,arcname=p.name,recursive=False)
with tarfile.open(archive,'r:gz') as tar:
 for p in sorted(raw.iterdir()):
  if p.is_file():assert tar.extractfile(p.name).read()==p.read_bytes(),p.name
controls=out/'all-controls.tar.gz';files=[p for directory in [q/'native-controls',q/'header-controls'] for p in sorted(directory.rglob('*')) if p.is_file()]
with tarfile.open(controls,'w:gz') as tar:
 for p in files:tar.add(p,arcname=str(p.relative_to(q)),recursive=False)
with tarfile.open(controls,'r:gz') as tar:
 for p in files:assert tar.extractfile(str(p.relative_to(q))).read()==p.read_bytes()
(out/'capture-index.json.gz').write_bytes(gzip.compress((json.dumps(index,indent=2)+'\n').encode(),mtime=0))
summary=dict(selected=d['selected'],attempted=d['completed'],native_cases=d['captured_cases'],counts=d['counts'],captures=len(d['captures']),partition=d['partition'],unexecuted=d['unexecuted'],manifest_sha256=d['manifest_sha256'],runner_sha256=d['runner_sha256'],selected_sha256=d['selected_sha256'],archive_sha256=hashof(archive),controls_archive_sha256=hashof(controls),program_sha256=d['program_sha256'],engine_library_sha256=d['engine_library_sha256'],whole_confirmed=1369+d['counts']['pass'],denominator=3805)
(out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(summary);print('header sources',len(header_sources),'unchanged RGB',preserved_pixels,'changed',len(changed_pixels))
