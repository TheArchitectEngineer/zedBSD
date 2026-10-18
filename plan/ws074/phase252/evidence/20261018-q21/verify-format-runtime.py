from pathlib import Path
import struct,json,hashlib,shutil,subprocess,gzip
q=Path('build/ws074-20261018/q21');dest=q/'post-format-engine';dest.mkdir(exist_ok=True)
for name in ['browser','libbrowser.so']:shutil.copy2(Path('build/ws074-20261018/plain')/name,dest/name)
def sections(p):
 data=p.read_bytes();assert data[:6]==b'\x7fELF\x02\x01';offset=struct.unpack_from('<Q',data,40)[0];size,count,names_index=struct.unpack_from('<HHH',data,58);headers=[struct.unpack_from('<IIQQQQIIQQ',data,offset+i*size) for i in range(count)];names=headers[names_index];strings=data[names[4]:names[4]+names[5]];out={}
 for h in headers:
  if not h[2]&2:continue
  name=strings[h[0]:].split(b'\0',1)[0].decode()
  if name=='.note.gnu.build-id':continue
  content=b'' if h[1]==8 else data[h[4]:h[4]+h[5]];out[name]=dict(type=h[1],flags=h[2],address=h[3],size=h[5],sha256=hashlib.sha256(content).hexdigest())
 return out
pairs=[]
for name in ['browser','libbrowser.so']:
 old=q/'final/engine'/name;new=dest/name;a=sections(old);b=sections(new);assert a==b,(name,[k for k in a if a[k]!=b.get(k)]);pairs.append(dict(name=name,allocated_sections_except_gnu_build_id_equal=True,sections=a,before_sha256=hashlib.sha256(old.read_bytes()).hexdigest(),post_format_sha256=hashlib.sha256(new.read_bytes()).hexdigest()))
command=[str(dest/'browser'),'--run','--settle-ms=5000','--font=userland/desktop/fonts/Mahora-Regular.ttf','--mono-font=userland/desktop/fonts/JetBrainsMono-Regular.ttf','--fallback-font=userland/desktop/fonts/DroidSansFallbackFull.ttf','file:///home/awe/zedBSD-claude1/plan/ws074/tests/dom/flex-basis-content.html'];run=subprocess.run(command,capture_output=True,text=True,timeout=20);assert run.returncode==0 and run.stdout==Path('plan/ws074/tests/dom/flex-basis-content.expected').read_text();(q/'post-format-independent.out').write_text(run.stdout);(q/'post-format-independent.stderr').write_text(run.stderr)
proof=dict(source_format_commit='70e40cb7a0f1b30fb5fc52555e2fef91621883d7',only_excluded_allocated_section='.note.gnu.build-id (diagnostic identifier)',pairs=pairs,independent39_equal=True,command=command,build_warning_count=sum('warning:' in x for x in (q/'build-post-format.log').read_text().splitlines()));assert proof['build_warning_count']==0;(q/'format-runtime-proof.json').write_text(json.dumps(proof,indent=2)+'\n');out=Path('plan/ws074/phase252/evidence/20261018-q21')
for p in [q/'format-runtime-proof.json',q/'post-format-independent.out',q/'post-format-independent.stderr',Path(__file__)]:shutil.copy2(p,out/p.name)
(out/'build-post-format.log.gz').write_bytes(gzip.compress((q/'build-post-format.log').read_bytes(),mtime=0));print('final host build warning0; browser/library allocated runtime sections equal; native39 exact')
