"""ASCII render of a .map file region: python tools/showmap.py maps/x.map [x0 y0 x1 y1]"""
import sys
path = sys.argv[1]
W = H = 0; hk = {}; vk = {}; gaps = {}; dr = {}
for line in open(path):
    p = line.split()
    if not p: continue
    if p[0] == 'MAP': W, H = int(p[1]), int(p[2])
    elif p[0] == 'TILE': gaps[(int(p[1]), int(p[2]))] = (int(p[3]), int(p[4]))
    elif p[0] == 'EDGE':
        i, k, pid = int(p[1]), int(p[2]), int(p[3]); r, c = divmod(i, W + 1)
        sym = '#' if k == 1 else ('P' if k == 2 else ' ')
        if r % 2 == 0: hk[(c, (r // 2) % H)] = sym
        else: vk[(c % W, (r - 1) // 2)] = sym
    elif p[0] == 'DRAGON':
        t, n = int(p[1]), int(p[2]); xs = list(map(int, p[3:3 + 2 * n]))
        for j in range(n): dr[(xs[2 * j], xs[2 * j + 1])] = ('A' if t == 0 else 'B') if j == 0 else ('a' if t == 0 else 'b')
x0, y0, x1, y1 = (map(int, sys.argv[2:6]) if len(sys.argv) > 5 else (0, 0, W - 1, H - 1))
for y in range(y0, y1 + 1):
    top = ''.join('+' + ('--' if hk.get((x, y), ' ') == '#' else ('PP' if hk.get((x, y)) == 'P' else '  ')) for x in range(x0, x1 + 1))
    mid = ''
    for x in range(x0, x1 + 1):
        v = vk.get((x, y), ' ')
        wall = '|' if v == '#' else ('P' if v == 'P' else ' ')
        g = gaps.get((x, y), (0, 0))
        cell = dr.get((x, y), '*' if g[1] and g[1] <= 10 else ('.' if g[1] else ' '))
        mid += wall + cell + ' '
    print(f"   {top}+")
    print(f"{y:2d} {mid}")
print('    ' + ''.join(f"{x % 100:<3d}" for x in range(x0, x1 + 1)))
