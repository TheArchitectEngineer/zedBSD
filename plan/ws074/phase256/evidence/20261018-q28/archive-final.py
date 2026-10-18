from pathlib import Path
import json,gzip,hashlib,tarfile,subprocess,collections,shutil
from PIL import Image
q=Path('build/ws074-20261018/q28');v=q/'final';out=Path('plan/ws074/phase256/evidence/20261018-q28');out.mkdir(parents=True,exist_ok=True)
def hashof(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def read_harness(root,old=False):
 rows={};files=[];entries=[]
 if old:
  for r in json.loads((root/'results.json').read_text()):rows[r['id']]=json.loads(gzip.decompress((root/r['report']).read_bytes()))
  return rows,files
 for d in sorted(root.glob('shard-*')):
  if not d.is_dir():continue
  report=json.loads((d/'report.json').read_text());assert report['eligible']==1508
  assert report['eligible_sha256']=='82147e6edaec1b3b8d537e46eb71b64f2f03132cc12803e284c6ca72377779e7'
  checkpoints=[json.loads(line) for line in (d/'progress.jsonl').read_text().splitlines()];assert [r for r in checkpoints if r['kind']=='result']==report['results']
  files.extend([d/'report.json',d/'progress.jsonl'])
  for r in report['results']:
   p=d/r['report'];data=gzip.decompress(p.read_bytes());assert p.with_suffix('').read_bytes()==data;record=json.loads(data);assert record['test_id']==r['test_id'] and record['outcome']==r['outcome'];assert r['test_id'] not in rows
   rows[r['test_id']]=record;files.append(p);entries.append(dict(id=r['test_id'],outcome=r['outcome'],report=str(p.relative_to(root))))
   if 'generated' in record:
    raw=Path(record['generated']['raw_directory']);files.extend(p for p in raw.rglob('*') if p.is_file());assert json.loads((raw/'provider.json').read_text())==record['generated']
    assert (raw/'stdout.bin').read_bytes().decode('utf-8',errors='replace')==record['stdout']
    assert not record['generated']['unsupported']
 retry_index=root/'reviewed-retries.json'
 if retry_index.exists():
  files.extend([retry_index,root/'retry-proof.json',root/'retry-00078.log'])
  for retry in json.loads(retry_index.read_text()):
   key=retry['id'];assert rows[key]['outcome']==retry['initial_outcome']=='process_timeout'
   path=root/retry['final_report'];data=gzip.decompress(path.read_bytes());assert path.with_suffix('').read_bytes()==data;record=json.loads(data);assert record['test_id']==key and record['outcome']==retry['final_outcome']=='fail';assert record['settle_ms']==5000 and record['process_timeout']==20 and retry['old_callback_and_completion_equal']
   rows[key]=record;files.extend(p for p in (root/retry['retry_directory']).rglob('*') if p.is_file())
   entry=next(e for e in entries if e['id']==key);entry['initial_report']=entry['report'];entry['initial_outcome']=entry['outcome'];entry['report']=retry['final_report'];entry['outcome']=record['outcome'];entry['reviewed_retry']='reviewed-retries.json'
 (root/'results.json').write_text(json.dumps(entries,indent=2)+'\n');files.extend([root/'results.json',root/'runs.json']);return rows,files
old,_=read_harness(Path('build/ws074-20261018/q27/final/after-harness'),True);new,hfiles=read_harness(v/'after-harness');assert len(new)==1508 and set(new)==set(old)
changes=[];callbacks=[];gains=[];lost=[]
for key,a in old.items():
 b=new[key]
 if a['outcome']!=b['outcome']:changes.append([key,a['outcome'],b['outcome']])
 if a['outcome']=='pass' and b['outcome']!='pass':lost.append(key)
 if a['outcome']!='pass' and b['outcome']=='pass':gains.append(key)
 if a['result_callbacks']!=b['result_callbacks'] or a['completion']!=b['completion']:callbacks.append(key)
assert not lost
h=dict(eligible=1508,eligible_sha256=hashlib.sha256('\n'.join(sorted(new)).encode()).hexdigest(),whole_changes=changes,whole_regressions=lost,whole_gains=gains,callback_or_completion_changes=callbacks,counts=dict(collections.Counter(r['outcome'] for r in new.values())),callbacks=dict(collections.Counter(str(c['status']) for r in new.values() for c in r['result_callbacks'])))
h['reviewed_retries']=json.loads((v/'after-harness/reviewed-retries.json').read_text()) if (v/'after-harness/reviewed-retries.json').exists() else []
(out/'harness-comparison.json').write_text(json.dumps(h,indent=2)+'\n')
a=json.loads(Path('build/ws074-20261018/q21/verified/after-static/report.json').read_text());b=json.loads((v/'after-static/report.json').read_text());sa={r['test']:r for r in a['results']};sb={r['test']:r for r in b['results']};assert len(sa)==1561 and set(sa)==set(sb);assert a['commit']==b['commit'] and a['viewport']==b['viewport']
checkpoints=[json.loads(line)['record'] for line in (v/'after-static/progress.jsonl').read_text().splitlines() if '"kind": "result"' in line];assert checkpoints==b['results']
s=dict(eligible=1561,eligible_sha256=hashlib.sha256('\n'.join(sorted(sb)).encode()).hexdigest(),counts=dict(collections.Counter(r['status'] for r in sb.values())),whole_gains=[k for k in sa if sa[k]['status']!='pass' and sb[k]['status']=='pass'],whole_regressions=[k for k in sa if sa[k]['status']=='pass' and sb[k]['status']!='pass'],pixel_metric_changes=[k for k in sa if [(r.get('path'),r.get('relation'),r.get('different_pixels'),r.get('max_difference')) for r in sa[k]['references']]!=[(r.get('path'),r.get('relation'),r.get('different_pixels'),r.get('max_difference')) for r in sb[k]['references']]])
assert not s['whole_regressions'];assert s['eligible_sha256']=='0f90e8fac9af5a97e11cce8ac0dfc16fc9360b1903031d742cf8bf1361f48d93';(out/'static-comparison.json').write_text(json.dumps(s,indent=2)+'\n')
scripted=json.loads((v/'scripted-413/report.json').read_text());prior=json.loads(Path('build/ws074-20261018/q21/verified/scripted-413/report.json').read_text());assert scripted['selected']==scripted['completed']==413 and not scripted['unexecuted'] and not scripted['source_changed_during_run'];assert scripted['runner_sha256']==hashof('plan/ws074/tests/run-interop2025-scripted-reftests.py')
pa={r['test_id']:r for r in prior['results']};pb={r['test_id']:r for r in scripted['results']};assert set(pa)==set(pb)
scregress=[k for k in pa if pa[k]['outcome']=='pass' and pb[k]['outcome']!='pass'];assert not scregress
priorraw=Path(prior['raw_directory']);raw=Path(scripted['raw_directory']);oldcaps={c['resource']:json.loads((priorraw/c['record']).read_text()) for c in prior['captures']};rgb_changes=[]
for capture in scripted['captures']:
 record=json.loads((raw/capture['record']).read_text());assert record['outcome']==capture['outcome']
 if record['outcome']=='pass':assert record['capture_boundary']['valid'] and record['readiness']['status']=='ready' and not record['unsupported']
 if record.get('png'):
  assert hashof(record['png']['file'])==record['png']['sha256']
  with Image.open(record['png']['file']) as image:assert image.size==(800,600) and hashlib.sha256(image.convert('RGB').tobytes()).hexdigest()==record['png']['rgb_sha256']
  if record['png']['rgb_sha256']!=oldcaps[capture['resource']]['png']['rgb_sha256']:rgb_changes.append(capture['resource'])
sc=dict(counts=scripted['counts'],whole_regressions=scregress,whole_changes=[k for k in pa if pa[k]['outcome']!=pb[k]['outcome']],captures=len(scripted['captures']),rgb_changes=rgb_changes);(out/'scripted-comparison.json').write_text(json.dumps(sc,indent=2)+'\n')
crash=json.loads((v/'crash-report.json').read_text());priorcrash=json.loads(Path('build/ws074-20261018/q21/verified/crash-report.json').read_text());ca={r['test_id']:r['outcome'] for r in priorcrash['results']};cb={r['test_id']:r['outcome'] for r in crash['results']};assert len(cb)==48 and set(ca)==set(cb);assert not [k for k in ca if ca[k]=='pass' and cb[k]!='pass'];cr=dict(counts=crash['counts'],changes={k:[ca[k],cb[k]] for k in ca if ca[k]!=cb[k]});(out/'crash-comparison.json').write_text(json.dumps(cr,indent=2)+'\n')
proof=json.loads((v/'source-proof.json').read_text());assert all(hashof(p)==digest for p,digest in proof.items());assert scripted['engine_library_sha256']==proof[str(v/'engine/libbrowser.so')];assert not subprocess.check_output(['git','-C','build/ws074-suites/wpt','status','--porcelain','--untracked-files=no']).strip()
archives={}
def archive(label,files,base):
 path=out/(label+'.tar.gz');mapping={str(p.relative_to(base.resolve())) if p.is_absolute() else str(p.relative_to(base)):p for p in files}
 with tarfile.open(path,'w:gz') as tar:
  for name,p in sorted(mapping.items()):tar.add(p,arcname=name,recursive=False)
 with tarfile.open(path,'r:gz') as tar:
  assert len(tar.getmembers())==len(mapping)
  for member in tar:
   assert tar.extractfile(member).read()==mapping[member.name].read_bytes()
 archives[label]=dict(sha256=hashof(path),members=len(mapping))
archive('harness-all-raw',hfiles,v/'after-harness');archive('scripted-all-native-captures',[p for p in raw.iterdir() if p.is_file()],raw);crraw=Path(crash['raw_directory']);archive('crash-all-raw',[p for p in crraw.iterdir() if p.is_file()],crraw)
archive('native-controls',[p for p in q.rglob('*') if p.is_file() and (p.suffix in ['.log','.out','.stderr','.html','.expected','.json','.c','.py'] or p.name.endswith('.txt')) and 'engine' not in p.parts and 'after-harness' not in p.parts and not any(part.startswith('after-static') for part in p.parts) and 'scripted-413' not in p.parts and 'data' not in p.parts],q)
for name,path in [('static-report.json',v/'after-static/report.json'),('static-progress.jsonl',v/'after-static/progress.jsonl'),('scripted-report.json',v/'scripted-413/report.json'),('crash-report.json',v/'crash-report.json')]: (out/(name+'.gz')).write_bytes(gzip.compress(path.read_bytes(),mtime=0))
for src in [v/'source-proof.json',q/'style-review.json',q/'style-generated.log',q/'versions.json',q/'target-commands-7.json',v/'native-runs.json',v/'native.out',v/'error-events.log',v/'error-reporting.log',v/'acid/report.json']:
 shutil.copy2(src,out/('acid-report.json' if src==v/'acid/report.json' else src.name))
for path in ['plan/ws074/tests/host-error-events.c','plan/ws074/tests/host-error-reporting.c','plan/ws074/tests/dom/error-reporting.html','plan/ws074/tests/dom/error-reporting.expected']:
 shutil.copy2(path,out/Path(path).name)
held='/performance-timeline/buffered-flag-with-entryTypes-observer.tentative.any.html';assert new[held]['outcome']=='pass'
controls=json.loads((q/'final-oracle-proof.json').read_text());assert len(controls)==4 and all(r['native']==r['chrome']==r['expected'] for r in controls)
prerequisite=(v/'prereq.out').read_text();assert 'prior-entry-count 1' in prerequisite and 'future-entry-count 1' in prerequisite and 'observed future-probe' in prerequisite
assert not gains, 'audit unexpected new whole gains before certification'

total=h['counts'].get('pass',0)+s['counts'].get('pass',0)+sc['counts'].get('pass',0)+cr['counts'].get('pass',0)
summary=dict(source_commit=(v/'source-commit.txt').read_text().strip(),harness=h,static=s,scripted=sc,crash=cr,raw_fixed_pass=total,verified_fixed_pass=total,denominator=3805,held_error_report_fidelity=0,prior_held_revalidated=held,unattempted_harness=275,archives=archives,dom_cases=40,runtime_error_independent_conditions=16,runtime_error_chrome_exact=16,native_error_event_gc_cases=9,native_error_reporting_gc_cases=19,native_mark_gc_cases=7,native_observer_gc_cases=10,target_sources=7,target_warnings=0,scoped_clearance_only=True,whole_phase_uncleared=True)
summary['whole_phase_remaining']='Unknown/external/eval/Function/module/worker source provenance, syntax/global reporting, content handler named parameters and full target/p100 remain pending.'
summary['official_failure_controls']=controls
(out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');shutil.copy2(__file__,out/'archive-final.py');print(json.dumps(summary,indent=2))
