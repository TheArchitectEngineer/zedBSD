#!/usr/bin/env python3
# ws091-p003: heuristic checks of plan/coding-style.md rules that plan/tools/style-check.py does not cover:
# conditions of three or more clauses (or mixed && and ||) on one line (6), returns of a call result (11),
# Booleans built from an expression (6), nested or split conditional operators (6), (void) casts of parameters
# (4, UNUSED_PARAMETER), split calls with two arguments on a line (8), and public/static definitions and
# file-scope declarations without a comment (2, 3).  Heuristic: every hit needs reading (table initializers of
# several numbers a row are hits too).
#   python3 plan/ws091/tests/style-extra.py FILE...
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import re
import sys

def strip(lines):
    out = []
    inc = False
    for line in lines:
        s = []
        i = 0
        while i < len(line):
            if inc:
                e = line.find("*/", i)
                if e < 0:
                    i = len(line)
                    continue
                i = e + 2
                inc = False
                continue
            if line.startswith("/*", i):
                inc = True
                i += 2
                continue
            if line.startswith("//", i):
                break
            c = line[i]
            if c in "\"'":
                q = c
                s.append(q)
                i += 1
                while i < len(line) and line[i] != q:
                    if line[i] == "\\":
                        i += 1
                    i += 1
                s.append(q)
                i += 1
                continue
            s.append(c)
            i += 1
        out.append("".join(s))
    return out

def cond_text(code, start):
    """Returns the condition of an if/while beginning at line start, joined."""
    text = ""
    depth = 0
    began = False
    for k in range(start, min(start + 12, len(code))):
        for ch in code[k]:
            if ch == "(":
                depth += 1
                began = True
            elif ch == ")":
                depth -= 1
            if began:
                text += ch
            if began and depth == 0:
                return text, k
        text += " "
    return text, start

def top_level_ops(cond):
    """Counts && and || at the top level of a condition (depth 1)."""
    depth = 0
    ands = 0
    ors = 0
    i = 0
    while i < len(cond):
        ch = cond[i]
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        elif cond.startswith("&&", i):
            ands += 1
            i += 1
        elif cond.startswith("||", i):
            ors += 1
            i += 1
        i += 1
    return ands, ors

def main():
    for path in sys.argv[1:]:
        raw = open(path).read().split("\n")
        code = strip(raw)
        for n, line in enumerate(code):
            no = n + 1
            m = re.match(r"^\s*(?:\} else )?(if|while) \(", line)
            if m:
                cond, end = cond_text(code, n)
                a = cond.count("&&")
                o = cond.count("||")
                if end == n and (a + o >= 2 or (a and o)):
                    print(f"{path}:{no}: cond-one-line: {raw[n].strip()}")
            if re.match(r"^\s*return [A-Za-z_]\w*\(", line) and not re.match(r"^\s*return (sizeof|defined)\(", line):
                print(f"{path}:{no}: return-call: {raw[n].strip()}")
            if re.match(r"^\s*[\w\->.\[\]]+ = [^;]*(==|!=|<=|>=|&&|\|\|| < | > )[^;]*;", line) and "?" not in line:
                print(f"{path}:{no}: bool-expr: {raw[n].strip()}")
            if re.match(r"^\s*[\w\->.\[\]]+ = !", line):
                print(f"{path}:{no}: bool-expr: {raw[n].strip()}")
            if line.count("?") >= 2:
                print(f"{path}:{no}: nested-ternary: {raw[n].strip()}")
            if "?" in line and not line.rstrip().endswith(";") and not line.rstrip().endswith(")") :
                print(f"{path}:{no}: ternary-split: {raw[n].strip()}")
            if re.match(r"^\s*\(void\)\w+;", line):
                print(f"{path}:{no}: void-cast: {raw[n].strip()}")
            # split call: a line ends with ',' and next line has a comma before its end
            st = line.rstrip()
            if st.endswith(",") and n + 1 < len(code) and not re.match(r"^\s*[\{\"]", code[n+1]):
                nxt = code[n + 1].rstrip()
                inner = nxt.rstrip(",;) ")
                depth = 0
                commas = 0
                for ch in inner:
                    if ch in "([{":
                        depth += 1
                    elif ch in ")]}":
                        depth -= 1
                    elif ch == "," and depth == 0:
                        commas += 1
                # also first line: call opened with args before the break
                if commas >= 1:
                    print(f"{path}:{no + 1}: split-args: {raw[n+1].strip()}")
                else:
                    mm = re.search(r"\w\((.*),$", st)
                    if mm and "," in mm.group(1) and not re.match(r"^\s*(static|const|struct|\{)", st):
                        print(f"{path}:{no}: split-args-first: {raw[n].strip()}")
            # function definition header comment
            if re.match(r"^[a-z_]\w*\($", line) and n >= 1:
                k = n - 1
                # return type lines
                while k >= 0 and code[k].strip() and not raw[k].strip().endswith("*/"):
                    k -= 1
                if k < 0 or not raw[k].strip().endswith("*/"):
                    print(f"{path}:{no}: no-function-comment: {raw[n].strip()}")
            # file-scope variable / type comment
            if re.match(r"^(static |const |struct \w+ \{|typedef|enum \w* ?\{|union)", raw[n]) and not raw[n].rstrip().endswith(";") or re.match(r"^static [^()]*;$", raw[n]) or re.match(r"^static [^(]*=", raw[n]):
                if re.match(r"^static [^;]*\([^;]*\);$", raw[n]):
                    continue
                if n >= 1 and raw[n - 1].strip().endswith("*/"):
                    continue
                if re.match(r"^(static|const)\s[^;{=]*$", raw[n]) and n + 1 < len(raw) and re.match(r"^\w+\($", raw[n+1]):
                    continue
                print(f"{path}:{no}: no-decl-comment: {raw[n].strip()}")

main()
