from pathlib import Path
import json,gzip,collections,hashlib
root=Path('build/ws074-20261018/q12')
def read(phase):
 out=root/(phase+'-harness');rows={};eligible_hashes=set();reasons=[]
 for d in sorted(out.glob('shard-*')):
  if not d.is_dir():continue
  report=json.loads((d/'report.json').read_text());records=[json.loads(s) for s in (d/'progress.jsonl').read_text().splitlines()];assert [x for x in records if x['kind']=='result']==report['results'];eligible_hashes.add(report['eligible_sha256']);assert report['eligible']==1455;reasons.append(report['reason_counts'])
  for x in report['results']:
   path=d/x['report'];data=gzip.decompress(path.read_bytes());assert path.with_suffix('').read_bytes()==data;raw=json.loads(data);assert raw['test_id']==x['test_id'];assert raw['outcome']==x['outcome'];assert raw['test_id'] not in rows;rows[raw['test_id']]=(raw,d.name+'/'+path.name)
 assert len(rows)==1455;assert len(eligible_hashes)==1;assert all(x==reasons[0] for x in reasons)
 return rows,eligible_hashes.pop(),reasons[0]
def callbacks(items):
 seen=collections.Counter();out={}
 for c in items:
  k=(c['name'],seen[c['name']]);seen[c['name']]+=1;out[k]=c
 return out
old,oh,oread=read('before');new,nh,nread=read('after');assert set(old)==set(new);assert oh==nh;assert oread==nread
up=[];down=[];missing=[];extra=[];order=[];wholeup=[];wholedown=[];counts={}
for phase,rows in [('before',old),('after',new)]:
 counts[phase]=dict(outcomes=dict(collections.Counter(x[0]['outcome'] for x in rows.values())),callbacks=dict(collections.Counter(str(c['status']) for x in rows.values() for c in x[0].get('result_callbacks',[]))))
 result=[dict(id=i,outcome=a['outcome'],subtests=len(a.get('result_callbacks',[])),first_failure=next((c.get('message','') for c in a.get('result_callbacks',[]) if c['status']!=0),''),report=p) for i,(a,p) in sorted(rows.items())];(root/(phase+'-harness')/'results.json').write_text(json.dumps(result,indent=2)+'\n')
for i,(a,p) in sorted(new.items()):
 o=old[i][0];oc=callbacks(o.get('result_callbacks',[]));nc=callbacks(a.get('result_callbacks',[]))
 for key,c in oc.items():
  if key not in nc:missing.append([i,key,c['status']])
  elif c['status']==0 and nc[key]['status']!=0:down.append([i,key,c.get('message'),nc[key].get('message')])
  elif c['status']!=0 and nc[key]['status']==0:up.append([i,key])
 extra.extend([i,k,c['status']] for k,c in nc.items() if k not in oc)
 if list(oc)!=list(nc):order.append(i)
 if o['outcome']=='pass' and a['outcome']!='pass':wholedown.append(i)
 if o['outcome']!='pass' and a['outcome']=='pass':wholeup.append(i)
s=dict(eligible=1455,eligible_sha256=nh,reasons=nread,counts=counts,new_pass=wholeup,whole_regressions=wholedown,callback_improvements=up,callback_regressions=down,missing=missing,extra=extra,name_order_changed=order);(root/'harness-comparison.json').write_text(json.dumps(s,indent=2)+'\n');print({k:v if k not in ['callback_improvements','callback_regressions','missing','extra'] else len(v) for k,v in s.items()});assert not wholedown and not down and not any(x[2]==0 for x in missing)
