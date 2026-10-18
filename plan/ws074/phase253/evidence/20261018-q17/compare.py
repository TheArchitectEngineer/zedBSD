from pathlib import Path
import json,gzip,collections,hashlib
root=Path('build/ws074-20261018/q17/final')
def read(phase):
 out=root/(phase+'-harness');rows={}
 if phase=='before':out=Path('build/ws074-20261018/q14/final/after-harness')
 if phase=='before':
  entries=json.loads((out/'results.json').read_text())
  for x in entries:rows[x['id']]=json.loads(gzip.decompress((out/x['report']).read_bytes()))
 else:
  entries=[]
  for d in sorted(out.glob('shard-*')):
   if not d.is_dir():continue
   report=json.loads((d/'report.json').read_text());records=[json.loads(s) for s in (d/'progress.jsonl').read_text().splitlines()];assert [x for x in records if x['kind']=='result']==report['results'];assert report['eligible']==1491
   for x in report['results']:
    path=d/x['report'];data=gzip.decompress(path.read_bytes());assert path.with_suffix('').read_bytes()==data;raw=json.loads(data);assert raw['test_id']==x['test_id'] and raw['outcome']==x['outcome'];assert raw['test_id'] not in rows;rows[raw['test_id']]=raw;entries.append(dict(id=raw['test_id'],outcome=raw['outcome'],report=str(path.relative_to(out))))
  (out/'results.json').write_text(json.dumps(entries,indent=2)+'\n')
 assert len(rows)==1491
 return rows
def callbacks(items):
 seen=collections.Counter();out={}
 for c in items:
  k=(c['name'],seen[c['name']]);seen[c['name']]+=1;out[k]=c
 return out
old=read('before');new=read('after');assert set(old)==set(new)
up=[];down=[];missing=[];extra=[];order=[];wholeup=[];wholedown=[];counts={}
for phase,rows in [('before',old),('after',new)]:counts[phase]=dict(outcomes=dict(collections.Counter(x['outcome'] for x in rows.values())),callbacks=dict(collections.Counter(str(c['status']) for x in rows.values() for c in x.get('result_callbacks',[]))))
for i,a in sorted(new.items()):
 o=old[i];oc=callbacks(o.get('result_callbacks',[]));nc=callbacks(a.get('result_callbacks',[]))
 for key,c in oc.items():
  if key not in nc:missing.append([i,key,c['status']])
  elif c['status']==0 and nc[key]['status']!=0:down.append([i,key,c.get('message'),nc[key].get('message')])
  elif c['status']!=0 and nc[key]['status']==0:up.append([i,key])
 extra.extend([i,k,c['status']] for k,c in nc.items() if k not in oc)
 if list(oc)!=list(nc):order.append(i)
 if o['outcome']=='pass' and a['outcome']!='pass':wholedown.append(i)
 if o['outcome']!='pass' and a['outcome']=='pass':wholeup.append(i)
s=dict(eligible=1491,eligible_sha256=hashlib.sha256('\n'.join(sorted(old)).encode()).hexdigest(),counts=counts,new_pass=wholeup,whole_regressions=wholedown,callback_improvements=up,callback_regressions=down,missing=missing,extra=extra,name_order_changed=order);(root/'harness-comparison.json').write_text(json.dumps(s,indent=2)+'\n');print({k:v if k not in ['callback_improvements','callback_regressions','missing','extra'] else len(v) for k,v in s.items()});assert not wholedown
