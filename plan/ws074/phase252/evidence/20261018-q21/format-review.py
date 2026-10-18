from pathlib import Path
import re,subprocess
r=Path('userland/desktop/libbrowser');q=Path('build/ws074-20261018/q21');scopes={'css/values.c':['values_flex_basis'],'css/cascade.c':['cascade_flex_basis'],'bind/computed.c':['computed_length'],'layout/flex.c':['flex_content_basis','flex_base_size']};chunks=[]
for name,functions in scopes.items():
 p=r/name;s=p.read_text()
 for fun in functions:
  m=re.search(r'(?:static [^\n]+\n)'+fun+r'\(\n.*?\n\}',s,re.S);assert m,fun
  code=m.group();formatted=subprocess.check_output(['clang-format-19','--style={BasedOnStyle: LLVM, UseTab: Always, IndentWidth: 8, TabWidth: 8, ColumnLimit: 0, BreakBeforeBraces: Linux, AllowShortIfStatementsOnASingleLine: Never}'],input=code.encode()).decode().rstrip()
  # The full standard overrides clang-format's one-line function signatures.
  first=formatted.index('{');signature=formatted[:first].strip();head=re.match(r'(static [^\n]+)\n(\w+)\((.*)\)',signature,re.S)
  if head:
   args=[x.strip() for x in head.group(3).split(',')];signature=head.group(1)+'\n'+head.group(2)+'(\n'+',\n'.join('\t'+x for x in args)+')\n';formatted=signature+formatted[first:]
  s=s[:m.start()]+formatted+s[m.end():];chunks.append('/* '+fun+' full generated scope. */\n'+formatted)
 p.write_text(s)
(q/'generated-functions.c').write_text('\n\n'.join(chunks)+'\n')
