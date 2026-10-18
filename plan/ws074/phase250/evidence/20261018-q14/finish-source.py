from pathlib import Path
import re,subprocess,json
functions={"css/values.c":["values_calc_value","values_calc_add","values_one_track"],"css/cascade.c":["cascade_calc","cascade_tracks"],"bind/computed.c":["computed_track_breadth"],"layout/grid.c":["grid_column_stretches"]}
for name,names in functions.items():
 p=Path("userland/desktop/libbrowser")/name;s=p.read_text()
 for name in names:
  match=re.search(r"(?m)^"+name+r"\(\n",s);assert match,name
  start=s.rfind("\n",0,match.start()-1)+1;opening=s.index("{",match.end());depth=1;end=opening+1
  while depth:
   if s[end]=="{":depth+=1
   if s[end]=="}":depth-=1
   end+=1
  text=s[start:end];signature=text[:text.index("{")]
  formatted=subprocess.run(["clang-format-19","--style={BasedOnStyle: LLVM, ColumnLimit: 0, IndentWidth: 8, ContinuationIndentWidth: 4, TabWidth: 8, UseTab: Always, SortIncludes: Never, BreakBeforeBraces: Linux, AlwaysBreakAfterReturnType: AllDefinitions}"],input=text,text=True,capture_output=True,check=True).stdout
  formatted=signature+formatted[formatted.index("{"):];s=s[:start]+formatted.rstrip()+s[end:]
 p.write_text(s)
