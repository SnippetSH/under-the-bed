"""프로토타입 점검 6차: data/ 수정 (5차 파이프라인 뒤에, 멱등).

- 모든 시나리오의 set_window: open = true/false → open_fraction = 1.0/0.0
- params: house.window_open_levels, fruit 5과위 배열, anthesis_jitter_days, rank_abortion_weight

    python tools/proto_data/pass6_data.py [RANK_FRACTION=...] [RANK_SIZE=...] [RANK_ABORT=...] [JITTER=3.0]
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pass2_data as p2  # noqa: E402

REPO = p2.REPO
TAG = p2.TAG

cfg = {
    "RANK_FRACTION": "[0.06, 0.11, 0.22, 0.39, 0.22]",
    "RANK_DELAY": "[0, 4, 7, 10, 12]",
    "RANK_SIZE": "[35, 25, 17, 12, 8]",
    "RANK_ABORT": "[0.1, 0.4, 1.0, 1.4, 1.8]",
    "JITTER": "3.0",
}
for a in sys.argv[1:]:
    k, v = a.split("=", 1)
    cfg[k] = v


def params() -> None:
    t = p2.read(p2.P)
    t = p2.insert_after(t, "house", "window_bypass_fraction",
                        "window_open_levels = [0, 0.3, 0.5, 0.7, 1.0]  # 허용 개방률. 디렉터 결정 (6차)")
    t = p2.insert_after(t, "fruit", "rank_anthesis_delay_days",
                        f"anthesis_jitter_days = {cfg['JITTER']}  # 과실마다 개화 시점 편차 (±, 일). 게임용 임의값 (6차)")
    t = re.sub(r"^anthesis_jitter_days = [^\n]*",
               f"anthesis_jitter_days = {cfg['JITTER']}  # 과실마다 개화 시점 편차 (±, 일). 게임용 임의값 (6차)",
               t, flags=re.M)
    t = p2.set_param(t, "fruit", "rank_fraction", cfg["RANK_FRACTION"])
    t = p2.set_param(t, "fruit", "rank_anthesis_delay_days", cfg["RANK_DELAY"])
    t = p2.set_param(t, "fruit", "rank_potential_fresh_g", cfg["RANK_SIZE"])
    t = re.sub(r"^rank_abortion_weight = [^\n]*",
               f"rank_abortion_weight = {cfg['RANK_ABORT']}  # 과위별 낙과 배율 (6차: 5과위). 게임용 임의값",
               t, flags=re.M)
    p2.write(p2.P, t)


def scenarios() -> None:
    for p in sorted((REPO / "data/scenarios").rglob("*.toml")):
        t = p2.read(p)
        t2 = re.sub(r"^open = true$", "open_fraction = 1.0", t, flags=re.M)
        t2 = re.sub(r"^open = false$", "open_fraction = 0.0", t2, flags=re.M)
        if t2 != t:
            p2.write(p, t2)


if __name__ == "__main__":
    params()
    scenarios()
    print("ok", cfg)
