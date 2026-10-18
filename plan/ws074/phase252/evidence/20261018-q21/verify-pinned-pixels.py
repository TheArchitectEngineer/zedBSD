from pathlib import Path
import ast,base64,io,json,hashlib,subprocess
q=Path('build/ws074-20261018/q21/verified');report=json.loads((q/'scripted-413/report.json').read_text());raw=Path(report['raw_directory'])
source=subprocess.check_output(['git','-C','build/ws074-suites/wpt','show','2d66b9b7998bb58c336138c178323ddee857b586:tools/wptrunner/wptrunner/executors/base.py'])
tree=ast.parse(source);selected=[n for n in ast.walk(tree) if isinstance(n,ast.FunctionDef) and n.name in ['check_pass','get_differences']];assert len(selected)==2
namespace=dict(io=io,base64=base64);exec(compile(ast.Module(body=selected,type_ignores=[]),'<pinned-wpt-comparison>','exec'),namespace)
class Logger:
 def info(self,*args,**kwargs):pass
Fixture=type('Fixture',(),dict(check_pass=namespace['check_pass'],get_differences=namespace['get_differences'],check_if_solid_color=lambda self,*args:None,logger=Logger()))
fixture=Fixture();count=0;verdicts=[]
for row in report['results']:
 captures={item['resource']:json.loads((raw/item['record']).read_text()) for item in row['captures']}
 for edge in row.get('edges',[]):
  lhs=captures[edge['source']];rhs=captures[edge['reference']];a=Path(lhs['png']['file']).read_bytes();b=Path(rhs['png']['file']).read_bytes();fuzzy=edge['fuzzy'];fuzzy=tuple(tuple(x) for x in fuzzy) if fuzzy is not None else None
  verdict,page=fixture.check_pass([[hashlib.sha256(a).hexdigest()],[hashlib.sha256(b).hexdigest()]],[[base64.b64encode(a).decode()],[base64.b64encode(b).decode()]],[edge['source'],edge['reference']],'==' if edge['relation']=='match' else '!=',fuzzy)
  assert verdict==edge['passed'],(row['test_id'],edge,verdict)
  count+=1;verdicts.append(dict(test_id=row['test_id'],source=edge['source'],reference=edge['reference'],passed=verdict))
result=dict(wpt_commit=report['wpt_commit'],path='tools/wptrunner/wptrunner/executors/base.py',source_sha256=hashlib.sha256(source).hexdigest(),functions=['check_pass','get_differences'],diagnostics_only_stubs=['logger.info','check_if_solid_color'],edges=count,all_verdicts_agree=True,verdicts=verdicts);(q/'pinned-pixel-verification.json').write_text(json.dumps(result,indent=2)+'\n');print('unmodified pinned WPT comparator agrees on',count,'captured edges')
