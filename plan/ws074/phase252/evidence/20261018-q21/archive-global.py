from pathlib import Path
import json,gzip,hashlib,tarfile,shutil,subprocess,collections
q=Path('build/ws074-20261018/q21/verified');out=Path('plan/ws074/phase252/evidence/20261018-q21');hashof=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
subprocess.run(['python3',str(q/'compare.py')],check=True);subprocess.run(['python3',str(q/'compare-static.py')],check=True)
h=json.loads((q/'harness-comparison.json').read_text());s=json.loads((q/'static-comparison.json').read_text());assert not h['whole_regressions'] and not s['whole_regressions'];assert not h['callback_regressions'] and not h['missing'] and not h['extra'] and not h['name_order_changed'];assert h['eligible_sha256']=='8eb1a5cacf64d34ab0b36d026b6f1335725ce9f9f886ba69b9c286bac8319727';assert s['eligible_sha256']=='0f90e8fac9af5a97e11cce8ac0dfc16fc9360b1903031d742cf8bf1361f48d93'
proof=json.loads((q/'source-proof.json').read_text());assert all(hashof(p)==v for p,v in proof.items());assert not subprocess.check_output(['git','-C','build/ws074-suites/wpt','status','--porcelain','--untracked-files=no']).strip()
root=q/'after-harness';files=[root/'runs.json',root/'results.json']
for d in sorted(root.glob('shard-*')):
 if not d.is_dir():continue
 files.extend([d/'report.json',d/'progress.jsonl']);files.extend(sorted(d.glob('test-*.json.gz')))
assert len([p for p in files if p.name.endswith('.json.gz')])==1491
archive=out/'harness-all-raw.tar.gz'
with tarfile.open(archive,'w:gz') as tar:
 for p in files:tar.add(p,arcname=str(p.relative_to(root)),recursive=False)
with tarfile.open(archive,'r:gz') as tar:
 for p in files:assert tar.extractfile(str(p.relative_to(root))).read()==p.read_bytes()
for name in ['harness-comparison.json','static-comparison.json','scripted-comparison.json','pinned-pixel-verification.json','verify-pinned-pixels.py','census.py','compare.py','compare-static.py','census.log','static.log','scripted.log','crash.log','acid.log']:
 shutil.copy2(q/name,out/name)
for name,source in [('static-report.json.gz',q/'after-static/report.json'),('static-progress.jsonl.gz',q/'after-static/progress.jsonl')]: (out/name).write_bytes(gzip.compress(source.read_bytes(),mtime=0))
shutil.copy2(q/'acid/report.json',out/'acid-report.json');shutil.copy2('build/ws074-20261018/q21/verify-reftests.py',out/'verify-reftests.py');shutil.copy2(__file__,out/'archive-global.py')
# Keep failed setup evidence separate from native outcomes; its server never opened.
invalid=Path('build/ws074-20261018/q21/final/after-harness');reports=list(invalid.glob('shard-*/report.json'));setup=dict(reason='sandbox EPERM socket.socket before browser invocation',native_results_valid=False,failed_harness_reports=len(reports),invalid_counts=dict(collections.Counter(x['outcome'] for p in reports for x in json.loads(p.read_text())['results'])))
(out/'invalid-setup-summary.json').write_text(json.dumps(setup,indent=2)+'\n');shutil.copy2('build/ws074-20261018/q21/final/static.log',out/'invalid-static-setup.log')
crash=json.loads((q/'crash-report.json').read_text());scripted=json.loads((q/'scripted-413/report.json').read_text());total=h['counts']['after']['outcomes'].get('pass',0)+s['after_counts'].get('pass',0)+crash['counts'].get('pass',0)+scripted['counts'].get('pass',0)
summary=dict(source_commit='063d87c951ce940f59b61750f1ef046ef9dfe8ff',source_format_commit='70e40cb7a0f1b30fb5fc52555e2fef91621883d7',native_runtime_equivalence_proof='format-runtime-proof.json',harness=h['counts'],static=dict(before=s['before_counts'],after=s['after_counts']),crash=crash['counts'],scripted=scripted['counts'],verified_pass=total,denominator=3805,percentage=total/3805*100,unexecuted=dict(harness=292,reftest=0),hashes={p.name:hashof(p) for p in [archive,out/'scripted-all-native-captures.tar.gz',out/'crash-all-raw.tar.gz',out/'harness-comparison.json',out/'static-comparison.json']})
(out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(summary)
