from pathlib import Path
import subprocess,json,gzip,hashlib,tarfile,shutil,collections
q=Path('build/ws074-20261018/q22');oldroot=Path('build/ws074-20261018/q21/verified/after-harness');out=Path('plan/ws074/phase189/evidence/20261018-q22');subprocess.run(['python3',str(q/'compare.py')],check=True)
h=json.loads((q/'harness-comparison.json').read_text());assert h['counts']['before']==h['counts']['after'];assert not any(h[k] for k in ['new_pass','whole_regressions','callback_improvements','callback_regressions','missing','extra','name_order_changed']);assert h['eligible_sha256']=='8eb1a5cacf64d34ab0b36d026b6f1335725ce9f9f886ba69b9c286bac8319727'
def read(root):
 return {x['id']:json.loads(gzip.decompress((root/x['report']).read_bytes())) for x in json.loads((root/'results.json').read_text())}
a=read(oldroot);b=read(q/'after-harness');assert a.keys()==b.keys();changes=[];message_changes=[]
for k,x in a.items():
 y=b[k];assert x['outcome']==y['outcome'],k;assert x['returncode']==y['returncode'],k
 old=[(z['name'],z['status']) for z in x.get('result_callbacks',[])];new=[(z['name'],z['status']) for z in y.get('result_callbacks',[])];assert old==new,(k,old,new)
 ca=x.get('completion');cb=y.get('completion');assert (ca is None)==(cb is None),k
 if ca is not None:
  assert ca['status']==cb['status'],k;assert [(z['name'],z['status']) for z in ca['tests']]==[(z['name'],z['status']) for z in cb['tests']],k
 for i,(l,r) in enumerate(zip(x.get('result_callbacks',[]),y.get('result_callbacks',[]))):
  if l.get('message')!=r.get('message'):message_changes.append(dict(test_id=k,index=i,name=l['name'],before=l.get('message'),after=r.get('message')))
proof=json.loads((q/'source-proof.json').read_text());assert all(hashlib.sha256(Path(p).read_bytes()).hexdigest()==v for p,v in proof.items());before=(q/'before-harness.py').read_text();after=Path('plan/ws074/tests/run-interop2025-harness.py').read_text();assert after==before.replace('worker = threading.Thread(target=server.serve_forever, daemon=True)','worker = threading.Thread(target=server.serve_forever, kwargs={"poll_interval": 0.01}, daemon=True)');assert not subprocess.check_output(['git','-C','build/ws074-suites/wpt','status','--porcelain','--untracked-files=no']).strip()
oldruns=json.loads((oldroot/'runs.json').read_text());runs=json.loads((q/'after-harness/runs.json').read_text());oldseconds=oldruns[-1]['seconds'];seconds=runs[-1]['seconds'];summary=dict(source_commit='7f66a68c1260673963b738c0065fd05995df6202',selected=1491,eligible_sha256=h['eligible_sha256'],outcomes=h['counts']['after']['outcomes'],callbacks=h['counts']['after']['callbacks'],all_per_id_outcomes_returncodes_callback_and_completion_statuses_equal=True,callback_messages_changed=len(message_changes),before_seconds=oldseconds,after_seconds=seconds,speedup=oldseconds/seconds,seconds_saved=oldseconds-seconds,native_sources_pair_fonts_unchanged=True,fixed_pass=1481,denominator=3805,unattempted_harness=292,classification_controls=5)
(q/'strict-equivalence.json').write_text(json.dumps(dict(summary=summary,callback_message_changes=message_changes),indent=2)+'\n');root=q/'after-harness';files=[root/'runs.json',root/'results.json']
for d in sorted(root.glob('shard-*')):
 if not d.is_dir():continue
 files.extend([d/'report.json',d/'progress.jsonl']);files.extend(sorted(d.glob('test-*.json.gz')))
assert len([p for p in files if p.name.endswith('.json.gz')])==1491
archive=out/'harness-all-raw.tar.gz'
with tarfile.open(archive,'w:gz') as tar:
 for p in files:tar.add(p,arcname=str(p.relative_to(root)),recursive=False)
with tarfile.open(archive,'r:gz') as tar:
 for p in files:assert tar.extractfile(str(p.relative_to(root))).read()==p.read_bytes()
for name in ['source-proof.json','strict-equivalence.json','harness-comparison.json','controls.log','census.py','census.log','compare.py']:
 shutil.copy2(q/name,out/name)
(out/'before-harness.py.gz').write_bytes(gzip.compress((q/'before-harness.py').read_bytes(),mtime=0));shutil.copy2(__file__,out/'archive.py');summary['archive_sha256']=hashlib.sha256(archive.read_bytes()).hexdigest();(out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(summary)
