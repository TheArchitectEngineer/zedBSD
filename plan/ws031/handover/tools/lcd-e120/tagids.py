import sys
p = sys.argv[1]; L = open(p).read().split("\n")
names = iter(["TLB-STUCK", "REL-TLB", "RTMAP-TLB"]); n = 0
for i, l in enumerate(L):
    if l.strip() == "tlb.expected_fault = 1;":
        L[i] = l + ' tlb.test_id = "%s";' % next(names); n += 1
    elif l.strip() == "tlb.expected_fault = 0;":
        L[i] = l + ' tlb.test_id = "lcdg-ktest";'
assert n == 3, n
open(p, "w").write("\n".join(L)); print("tagged")
