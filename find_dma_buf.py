import glob

for fn in glob.glob('recomp/batched/*.cpp'):
    with open(fn, 'r') as f:
        lines = f.readlines()
    for i, line in enumerate(lines):
        if '20692' in line or '0x50d4' in line.lower() or '20688' in line or '0x50d0' in line.lower():
            for j in range(i, -1, -1):
                if lines[j].startswith('void func_'):
                    print(f'{lines[j].split("(")[0]} at line {i+1} in {fn}: {lines[i].strip()}')
                    break
