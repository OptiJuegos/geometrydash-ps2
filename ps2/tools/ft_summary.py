import re,sys
def load(f):
    rows=[]
    for l in open(f):
        m=re.search(r"FT UPD: physics=([\d.]+)/([\d.]+) collision=([\d.]+)/([\d.]+) triggers=([\d.]+)/([\d.]+) objects=([\d.]+)/([\d.]+) particles=([\d.]+)/([\d.]+) create=([\d.]+)/([\d.]+) sort=([\d.]+)/([\d.]+)",l)
        if m: rows.append([float(x) for x in m.groups()])
    return [r for r in rows if r[10]>0.5]
for f in sys.argv[1:]:
    r=load(f); n=len(r)
    avg=lambda i: sum(x[i] for x in r)/n
    print(f,n,"windows: physics %.2f collision %.2f triggers %.2f objects %.2f create %.2f sort %.2f  | objects peak avg %.2f"%(avg(0),avg(2),avg(4),avg(6),avg(10),avg(12),avg(7)))
