"""M5b 5~6차: runs/<dir>/*.csv의 측정 창 통계 (창 안 수확 개수, 썩은 과실, 한낮 평균·최고 기온, 하우스 습도 평균, 끝 응애·천적·곰팡이 지수).

S28·S29는 창이 없어 전체 구간을 봐요. S31은 방사 후 6주(1008틱)가 창이고, 아래에 방사 시점과 +1, +3, +5, +6주 하우스 평균 응애 지수,
s31_none 대비 감소율을 더해 보여줘요.
    python tools/m5b_window_stats.py runs/pass6
"""
import csv, sys, pathlib

WIN = {"s26": 1968, "s27": 1968, "s28": 0, "s29": 0, "s30": 4872, "s31": 1800, "s32": 4128}
WIN_S31 = {"s31_none": 1800, "s31_none_dry": 1800, "s31_predator": 1800, "s31_predator_dry": 1800, "s31_predator_late": 3600}
WEEK = 168
d = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "runs/pass6")
print(f"{'name':20s} {'harv':>5s} {'rot':>4s} {'rot%':>5s} {'fruit@W':>8s} {'midT':>5s} {'maxT':>5s} {'rh_mean':>7s} {'mite_end':>8s} {'pred_end':>8s} {'pm_inf_end':>10s} {'bot_inf_end':>11s}")
data = {}
for f in sorted(d.glob("s*.csv")):
    rows = list(csv.DictReader(f.open(encoding="utf-8")))
    data[f.stem] = rows
    w = WIN_S31.get(f.stem, WIN[f.stem[:3]])
    at = lambda i, c: float(rows[i][c])
    end = min(len(rows) - 1, w + 6 * WEEK) if f.stem.startswith("s31") else len(rows) - 1  # S31은 방사 후 6주까지만
    harv = at(end, "harvest_total_count") - at(w, "harvest_total_count")
    rot = at(end, "ledger_fruit_rot_count") - at(w, "ledger_fruit_rot_count")
    win = rows[w:end + 1]
    mid = [float(r["house_air_temp_degC"]) for r in win if 11 <= int(r["hour"]) <= 15]
    rh = [float(r["house_rh_mean_index"]) for r in win]
    rotpct = 100 * rot / (harv + rot) if harv + rot else 0.0
    print(f"{f.stem:20s} {harv:5.0f} {rot:4.0f} {rotpct:5.1f} {at(w,'fruit_count'):8.0f} {sum(mid)/len(mid):5.1f} {max(float(r['house_air_temp_degC']) for r in win):5.1f} {sum(rh)/len(rh):7.2f} "
          f"{at(end,'spider_mite_mean_index'):8.3f} {at(end,'predator_mite_index'):8.3f} {at(end,'infection_powdery_mildew_index'):10.3f} {at(end,'infection_botrytis_index'):11.3f}")

if "s31_none" in data:
    print("\nS31 하우스 평균 응애 지수 (house_spider_mite_mean_index), 방사 시점 / +1 / +3 / +5 / +6주, 괄호는 s31_none 대비 감소율 %")
    base = data["s31_none"]
    for nm, rows in data.items():
        if not nm.startswith("s31"):
            continue
        w = WIN_S31[nm]
        vals = []
        for k in (0, 1, 3, 5, 6):
            i = w + k * WEEK
            m = float(rows[i]["house_spider_mite_mean_index"])
            b = float(base[i]["house_spider_mite_mean_index"])
            vals.append(f"{m:.3f}" + ("" if nm == "s31_none" or k not in (3, 5) else f" ({100 * (b - m) / b:+.0f}%)"))
        print(f"{nm:20s} " + "  ".join(f"{v:>14s}" for v in vals))
