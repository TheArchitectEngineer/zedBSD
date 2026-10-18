from pathlib import Path
import subprocess,json,os
root=Path('build/ws074-20261018/q24/idl');root.mkdir(exist_ok=True)
for n,identifier in enumerate(['/event-timing/idlharness.any.html','/storage-access-api/idlharness.window.html']):
 command=['python3','plan/ws074/tests/run-interop2025-harness.py','--manifest','plan/ws074/phase173/evidence/20261013-q1012/manifest.json','--program','/home/awe/zedBSD-claude1/build/ws074-20261018/q21/final/engine/browser','--test-id',identifier,'--out',str(root/('%d.json'%n)),'--font-dir','userland/desktop/fonts']
 with (root/('%d.log'%n)).open('w') as log:run=subprocess.run(command,stdout=log,stderr=log,timeout=30,env=dict(os.environ,XDG_DATA_HOME=str((root/'data').resolve())))
 record=json.loads((root/('%d.json'%n)).read_text());assert record['generated']['unsupported']==[]
 assert not any('Error fetching /interfaces/' in (x.get('message') or '') for x in record['result_callbacks'])
 assert any(x['path'].startswith('interfaces/') for x in record['generated']['assets'])
 print(identifier,record['outcome'],len(record['result_callbacks']),[(x['path'],x['sha256']) for x in record['generated']['assets'] if x['path'].startswith('interfaces/')],flush=True)
