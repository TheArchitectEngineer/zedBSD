from pathlib import Path
import json,gzip,hashlib,tarfile,subprocess,collections,shutil
q=Path('build/ws074-20261018/q24');out=Path('plan/ws074/phase189/evidence/20261018-q24');root=q/'after-harness'
oldroot=Path('build/ws074-20261018/q23/after-harness')
old={r['id']:json.loads(gzip.decompress((oldroot/r['report']).read_bytes())) for r in json.loads((oldroot/'results.json').read_text())}
old1491={r['id'] for r in json.loads(Path('build/ws074-20261018/q22/after-harness/results.json').read_text())}
rows={};files=[];entries=[]
for d in sorted(root.glob('shard-*')):
 if not d.is_dir():continue
 report=json.loads((d/'report.json').read_text());checkpoint=[json.loads(line) for line in (d/'progress.jsonl').read_text().splitlines()]
 assert [r for r in checkpoint if r['kind']=='result']==report['results'];assert report['eligible']==1508
 assert report['eligible_sha256']=='82147e6edaec1b3b8d537e46eb71b64f2f03132cc12803e284c6ca72377779e7'
 files.extend([d/'report.json',d/'progress.jsonl'])
 for r in report['results']:
  p=d/r['report'];data=gzip.decompress(p.read_bytes());assert p.with_suffix('').read_bytes()==data;record=json.loads(data)
  assert record['test_id']==r['test_id'] and record['outcome']==r['outcome'];assert r['test_id'] not in rows
  rows[r['test_id']]=record;entries.append(dict(id=r['test_id'],outcome=r['outcome'],report=str(p.relative_to(root))));files.append(p)
assert len(rows)==1508 and set(rows)==set(old)
assert hashlib.sha256('\n'.join(sorted(rows)).encode()).hexdigest()=='82147e6edaec1b3b8d537e46eb71b64f2f03132cc12803e284c6ca72377779e7'
(root/'results.json').write_text(json.dumps(entries,indent=2)+'\n');files.extend([root/'runs.json',root/'results.json'])
changed=[]
for identifier,a in old.items():
 b=rows[identifier]
 assert a['outcome']==b['outcome'] and a['returncode']==b['returncode'],identifier
 assert [(c['name'],c['status']) for c in a['result_callbacks']]==[(c['name'],c['status']) for c in b['result_callbacks']],identifier
 assert (a['completion'] is None)==(b['completion'] is None)
 if a['completion'] is not None:
  assert a['completion']['status']==b['completion']['status']
  assert [(c['name'],c['status']) for c in a['completion']['tests']]==[(c['name'],c['status']) for c in b['completion']['tests']]
 if [c.get('message') for c in a['result_callbacks']] != [c.get('message') for c in b['result_callbacks']]:changed.append(identifier)
assert set(changed)=={'/event-timing/idlharness.any.html','/storage-access-api/idlharness.window.html'}
assert not set(changed)&old1491
assets=[]
for identifier in sorted(set(rows)-old1491):
 record=rows[identifier];ctx=record['generated'];raw=Path(ctx['raw_directory'])
 assert ctx['unsupported']==[],(identifier,ctx['unsupported'])
 assert (raw/'stdout.bin').read_bytes().decode('utf-8',errors='replace')==record['stdout']
 assert (raw/'stderr.bin').read_bytes().decode('utf-8',errors='replace')==record['stderr']
 assert json.loads((raw/'provider.json').read_text())==ctx
 for path,digest in ctx['pinned_assets'].items():
  blob=subprocess.check_output(['git','-C','build/ws074-suites/wpt','show','2d66b9b7998bb58c336138c178323ddee857b586:'+path]);assert hashlib.sha256(blob).hexdigest()==digest
 for p in raw.rglob('*'):
  if p.is_file():files.append(p)
 for asset in ctx['assets']:
  if asset.get('provider')=='pinned-git-blob':assets.append(dict(test_id=identifier,**asset))
assert len(assets)==6 and len({a['path'] for a in assets})==5
for identifier in changed:
 r=rows[identifier];assert 'yield*' in r['stdout']+r['stderr'];assert 'WebIDL2 is not defined' in r['result_callbacks'][0]['message'];assert 'Error fetching' not in r['result_callbacks'][0]['message']
proof=json.loads((q/'source-proof.json').read_text());assert all(hashlib.sha256(Path(p).read_bytes()).hexdigest()==v for p,v in proof.items())
assert not subprocess.check_output(['git','-C','build/ws074-suites/wpt','status','--porcelain','--untracked-files=no']).strip()
archives={}
for label,paths in [('all1508-raw',files),('controls-and-idl',[p for base in [q/'controls',q/'idl'] for p in base.rglob('*') if p.is_file()])]:
 archive=out/(label+'.tar.gz');mapping={str(p.relative_to(q.resolve())) if p.is_absolute() else str(p.relative_to(q)):p for p in paths}
 with tarfile.open(archive,'w:gz') as tar:
  for name,p in sorted(mapping.items()):tar.add(p,arcname=name,recursive=False)
 with tarfile.open(archive,'r:gz') as tar:
  assert len(tar.getmembers())==len(mapping)
  for name,p in mapping.items():assert tar.extractfile(name).read()==p.read_bytes()
 archives[label]=dict(sha256=hashlib.sha256(archive.read_bytes()).hexdigest(),members=len(mapping))
summary=dict(source_commit='26980308b',eligible=1508,old1491_exact=True,whole_or_status_changes=0,callback_message_changes=changed,git_idl_assets=assets,counts=dict(collections.Counter(r['outcome'] for r in rows.values())),callbacks=dict(collections.Counter(str(c['status']) for r in rows.values() for c in r['result_callbacks'])),raw_fixed_pass=1482,verified_fixed_pass=1481,denominator=3805,held_native_false_positive=1,unattempted_harness=275,seconds=json.loads((root/'runs.json').read_text())[-1]['seconds'],archives=archives)
(out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
for name in ['census.py','census.log','source-proof.json','controls.log','classification.log','idl.py','idl.log']:
 shutil.copy2(q/name,out/name)
shutil.copy2(__file__,out/'archive.py')
print(json.dumps(summary,indent=2))
