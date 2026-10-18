from pathlib import Path
import json,collections,hashlib
root=Path('build/ws074-20261018/q17/final');before=Path('build/ws074-20261018/q14/final/after-static/report.json');after=root/'after-static/report.json';a=json.loads(before.read_text());b=json.loads(after.read_text());old={x['test']:x for x in a['results']};new={x['test']:x for x in b['results']};assert len(old)==1561 and set(old)==set(new);assert a['commit']==b['commit'];assert a['viewport']==b['viewport'];records=[json.loads(x)['record'] for x in (after.parent/'progress.jsonl').read_text().splitlines() if '"kind": "result"' in x];assert records==b['results']
up=[];down=[];changed=[]
for i,x in sorted(new.items()):
 o=old[i]
 if o['status']=='pass' and x['status']!='pass':down.append(i)
 if o['status']!='pass' and x['status']=='pass':up.append(i)
 if [(r.get('path'),r.get('relation'),r.get('different_pixels'),r.get('max_difference')) for r in o['references']]!=[(r.get('path'),r.get('relation'),r.get('different_pixels'),r.get('max_difference')) for r in x['references']]:changed.append(i)
s=dict(eligible=1561,eligible_sha256=hashlib.sha256('\n'.join(sorted(new)).encode()).hexdigest(),before_counts=dict(collections.Counter(x['status'] for x in old.values())),after_counts=dict(collections.Counter(x['status'] for x in new.values())),whole_improvements=up,whole_regressions=down,reference_pixel_changes=changed,baseline=str(before));(root/'static-comparison.json').write_text(json.dumps(s,indent=2)+'\n');print(s);assert not down
