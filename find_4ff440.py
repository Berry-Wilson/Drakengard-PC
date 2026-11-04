import glob

for fn in glob.glob('recomp/batched/*.cpp'):
    with open(fn, 'r') as f:
        lines = f.readlines()
    for i, line in enumerate(lines):
        if '4294964288' in line:
            for j in range(i, -1, -1):
                if lines[j].startswith('void func_'):
                    print(f'{lines[j].split("(")[0]} at line {i+1} in {fn}')
                    break
