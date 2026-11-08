import re,sys
# Regenerates exports/glide2x.txt and exports/glide3x.txt from the 3dfx Glide SDK headers
# (e.g. https://github.com/sezero/glide):
#   python3 tools/parse_glide.py glide2x/{cvg,sst1,h3}/glide/src/glide{,utl}.h  -> name argbytes
# then prefix "_" and suffix "@argbytes" per line (see exports/*.txt).
names={}
for f in sys.argv[1:]:
    s=open(f,errors='ignore').read()
    s=re.sub(r'/\*.*?\*/','',s,flags=re.S)
    for m in re.finditer(r'FX_ENTRY\s+([\w\s\*]+?)\s+FX_CALL\s*\n?\s*(\w+)\s*\((.*?)\)\s*;',s,re.S):
        name,args=m.group(2),m.group(3).strip()
        if args in('','void'): n=0
        else:
            parts=[a for a in args.split(',')]
            n=0
            for a in parts:
                a=a.strip()
                n+=8 if re.match(r'(double|FxI64|FxU64)\b',a) and '*' not in a else 4
        names.setdefault(name,set()).add(n); continue
        names[name]=n
[print(k,n) for k in sorted(names) for n in sorted(names[k])]
