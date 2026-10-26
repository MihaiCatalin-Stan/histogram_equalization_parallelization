# Summarizes runs.txt (one PROFILE line per run): median/min per step.
# Usage: summarize.py <name> <runs> <runs-file>
import statistics, sys
rows = [l.split()[1:] for l in open(sys.argv[3])]
w, h = rows[0][0], rows[0][1]
cols = list(zip(*[[float(x) for x in r[2:]] for r in rows]))
labels = ["read", "alloc output", "1 histogram", "2+3 cdf/cdf_min",
          "4 mapping", "5 apply mapping", "write"]
med = [statistics.median(c) for c in cols]
mn = [min(c) for c in cols]
total = sum(med)
print(f"{sys.argv[1]}: {w}x{h} ({int(w)*int(h)/1e6:.1f} MP), median of {sys.argv[2]} runs")
print(f"{'step':<18}{'median ms':>12}{'min ms':>12}{'% total':>10}")
for l, m, n in zip(labels, med, mn):
    print(f"{l:<18}{m:>12.5f}{n:>12.5f}{100*m/total:>9.1f}%")
print(f"{'total':<18}{total:>12.5f}")
