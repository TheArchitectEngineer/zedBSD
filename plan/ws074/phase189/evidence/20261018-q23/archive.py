from pathlib import Path
import json,gzip,hashlib,tarfile,subprocess,shutil,collections,importlib.util
q=Path('build/ws074-20261018/q23');out=Path('plan/ws074/phase189/evidence/20261018-q23');root=q/'after-harness';oldroot=Path('build/ws074-20261018/q22/after-harness');selection=json.loads((q/'selection-proof.json').read_text());inventory=json.loads((out/'input-inventory.json').read_text());new_ids={x['id'] for x in inventory};old={x['id']:json.loads(gzip.decompress((oldroot/x['report']).read_bytes())) for x in json.loads((oldroot/'results.json').read_text())};rows={};entries=[];files=[root/'runs.json']
for directory in sorted(root.glob('shard-*')):
 if not directory.is_dir():continue
 report=json.loads((directory/'report.json').read_text());checkpoints=[json.loads(x) for x in (directory/'progress.jsonl').read_text().splitlines()];assert [x for x in checkpoints if x['kind']=='result']==report['results'];assert report['eligible']==1508 and report['eligible_sha256']==selection['eligible_sha256'];files.extend([directory/'report.json',directory/'progress.jsonl'])
 for row in report['results']:
  p=directory/row['report'];data=gzip.decompress(p.read_bytes());assert p.with_suffix('').read_bytes()==data;record=json.loads(data);assert record['test_id']==row['test_id'] and record['outcome']==row['outcome'];assert row['test_id'] not in rows;rows[row['test_id']]=record;entries.append(dict(id=row['test_id'],outcome=row['outcome'],report=str(p.relative_to(root))));files.append(p)
assert len(rows)==1508 and set(rows)==set(old)|new_ids and not set(old)&new_ids;assert hashlib.sha256('\n'.join(sorted(rows)).encode()).hexdigest()==selection['eligible_sha256'];(root/'results.json').write_text(json.dumps(entries,indent=2)+'\n');files.append(root/'results.json')
message_changes=[]
for identifier,a in old.items():
 b=rows[identifier];assert a['outcome']==b['outcome'] and a['returncode']==b['returncode'],identifier
 assert [(x['name'],x['status']) for x in a.get('result_callbacks',[])]==[(x['name'],x['status']) for x in b.get('result_callbacks',[])],identifier
 ca=a.get('completion');cb=b.get('completion');assert (ca is None)==(cb is None),identifier
 if ca is not None:
  assert ca['status']==cb['status'];assert [(x['name'],x['status']) for x in ca['tests']]==[(x['name'],x['status']) for x in cb['tests']],identifier
 for aitem,bitem in zip(a.get('result_callbacks',[]),b.get('result_callbacks',[])):
  if aitem.get('message')!=bitem.get('message'):message_changes.append(identifier)
assert not message_changes
proof=json.loads((q/'source-proof.json').read_text());assert all(hashlib.sha256(Path(p).read_bytes()).hexdigest()==v for p,v in proof.items());assert not subprocess.check_output(['git','-C','build/ws074-suites/wpt','status','--porcelain','--untracked-files=no']).strip()
spec=importlib.util.spec_from_file_location('generated_archive_protocol','plan/ws074/tests/run-interop2025-harness.py');tool=importlib.util.module_from_spec(spec);spec.loader.exec_module(tool);contract=tool.generated_contract(Path('build/ws074-suites/wpt'));newrows=[];rawfiles=[]
for identifier in sorted(new_ids):
 record=rows[identifier];context=record['generated'];raw=Path(context['raw_directory']);assert record['command'][0]=='/home/awe/zedBSD-claude1/build/ws074-20261018/q21/final/engine/browser';assert record['settle_ms']==5000 and record['process_timeout']==20;assert (raw/'stdout.bin').read_bytes().decode('utf-8',errors='replace')==record['stdout'];assert (raw/'stderr.bin').read_bytes().decode('utf-8',errors='replace')==record['stderr'];assert json.loads((raw/'provider.json').read_text())==context
 source=next(x['source'] for x in inventory if x['id']==identifier);blob=subprocess.check_output(['git','-C','build/ws074-suites/wpt','show',tool.WPT_COMMIT+':'+source]);assert blob==(raw/'source.js').read_bytes();body,expected=tool.generated_wrapper(identifier,'/'+source,blob,contract);assert body==(raw/'wrapper.html').read_bytes();assert hashlib.sha256(body).hexdigest()==context['wrapper_sha256'];assert context['contract_sources']==contract['provenance']
 for path,digest in context['pinned_assets'].items():
  value=subprocess.check_output(['git','-C','build/ws074-suites/wpt','show',tool.WPT_COMMIT+':'+path]);assert hashlib.sha256(value).hexdigest()==digest
 newrows.append(dict(test_id=identifier,outcome=record['outcome'],native_outcome=record['native_outcome'],callbacks=len(record['result_callbacks']),unsupported=context['unsupported'],source=source,source_sha256=context['source_sha256'],wrapper_sha256=context['wrapper_sha256'],assets=context['pinned_assets'],rewrites=context['rewrites'],raw_directory=str(raw),returncode=record['returncode']))
 for p in sorted(raw.rglob('*')):
  if p.is_file():rawfiles.append((p,str(p.relative_to(root.resolve()))))
# The WPT assertion passes with two existing no-op product APIs; preserve it but do not certify its absent input.
held='/performance-timeline/buffered-flag-with-entryTypes-observer.tentative.any.html';assert rows[held]['outcome']=='pass';assert (q/'performance-prereq/native.out').read_text()=='mark-return undefined\nprior-entry-count 0\nfuture-entry-count 0\nobserved \n';chrome=(q/'performance-prereq/chrome.out').read_text();assert 'prior-entry-count 1' in chrome and 'future-entry-count 1' in chrome and 'observed future-probe' in chrome
holdback=dict(test_id=held,raw_wpt_outcome='pass',verified_outcome='uncleared_native_prerequisite',reason='performance.mark records nothing, getEntries returns empty, observer observe is a product no-op; WPT prior-entry prerequisite is absent',origin_phase='ws074-p189',needed_outputs=['real native mark records/retrieval','real observer future-entry delivery and correct buffered behavior'],native_probe='performance-prereq/native.out',chrome_probe='performance-prereq/chrome.out',raw_records_unchanged=True)
(out/'native-prerequisite-holdback.json').write_text(json.dumps(holdback,indent=2)+'\n');(out/'new17.json').write_text(json.dumps(newrows,indent=2)+'\n');comparison=dict(old_subset=1491,whole_status_and_returncode_changes=[],all_callback_and_completion_statuses_order_equal=True,callback_messages_changed=message_changes,new17_counts=dict(collections.Counter(x['outcome'] for x in newrows)),new17_callback_counts=dict(collections.Counter(str(x['status']) for k in new_ids for x in rows[k]['result_callbacks'])),old_subset_counts=dict(collections.Counter(x['outcome'] for x in old.values())),source_pair_fonts_unchanged=True);(out/'old1491-comparison.json').write_text(json.dumps(comparison,indent=2)+'\n')
archive=out/'all1508-raw.tar.gz'
with tarfile.open(archive,'w:gz') as tar:
 for p in files:tar.add(p,arcname='reports/'+str(p.relative_to(root)),recursive=False)
 for p,name in rawfiles:tar.add(p,arcname='generated/'+name,recursive=False)
with tarfile.open(archive,'r:gz') as tar:
 for p in files:assert tar.extractfile('reports/'+str(p.relative_to(root))).read()==p.read_bytes()
 for p,name in rawfiles:assert tar.extractfile('generated/'+name).read()==p.read_bytes()
control_files=[p for p in (q/'controls').rglob('*') if p.is_file()];controls=out/'all-controls.tar.gz'
with tarfile.open(controls,'w:gz') as tar:
 for p in sorted(control_files):tar.add(p,arcname=str(p.relative_to(q/'controls')),recursive=False)
with tarfile.open(controls,'r:gz') as tar:
 for p in sorted(control_files):assert tar.extractfile(str(p.relative_to(q/'controls'))).read()==p.read_bytes()
for name in ['probe.html','native.out','native.stderr','chrome.out','chrome.log','chrome-dom.html']:
 target=out/'performance-prereq'/name;target.parent.mkdir(exist_ok=True);shutil.copy2(q/'performance-prereq'/name,target)
for name in ['source-proof.json','census.py','census.log']:
 shutil.copy2(q/name,out/name)
shutil.copy2(__file__,out/'archive.py');shutil.copy2('userland/desktop/libbrowser/bind/prelude-source.inc',out/'observed-engine-prelude.inc');shutil.copy2('userland/desktop/libbrowser/bind/environment.c',out/'observed-engine-environment.c')
summary=dict(source_commit='feafc812e81f38fcd8138a79cd53b8728fff375c',eligible=1508,eligible_sha256=selection['eligible_sha256'],old_subset=1491,new_native_attempts=17,new_counts=comparison['new17_counts'],raw_harness_counts=dict(collections.Counter(x['outcome'] for x in rows.values())),raw_fixed_pass=1482,verified_fixed_pass=1481,held_false_positive_count=1,denominator=3805,unattempted_harness=275,callbacks=dict(collections.Counter(str(c['status']) for x in rows.values() for c in x['result_callbacks'])),new_callbacks=comparison['new17_callback_counts'],native_sources_pair_unchanged=True,primary_native_controls=13,classification_controls=5,seconds=json.loads((root/'runs.json').read_text())[-1]['seconds'],archive_sha256=hashlib.sha256(archive.read_bytes()).hexdigest(),controls_sha256=hashlib.sha256(controls.read_bytes()).hexdigest());(out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(summary)
