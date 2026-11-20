"""M5b: 시나리오 TOML을 schema_version 7로 올려요 (foliar-disease 8장).

- schema_version = 7
- [house] drip_installed = true (이미 있으면 그대로)
- [microbes] anthracnose = 0.0 (이미 있으면 그대로)
- [foliar] 섹션: botrytis_background_index = 0.0, powdery_mildew_background_index = 0.0 (이미 있으면 그대로)

멱등이고 주석·나머지 줄은 건드리지 않아요. UTF-8 (BOM 없음).
사용: python tools/migrate_scenarios_m5b.py [루트 폴더, 기본 data/scenarios]
"""
import pathlib
import re
import sys


def section_bounds(lines, header):
    """header 줄 번호와, 그 섹션의 마지막 key = value 줄 번호를 돌려줘요. 없으면 (None, None)."""
    start = None
    for i, ln in enumerate(lines):
        if ln.strip().split("#")[0].strip() == header:
            start = i
            break
    if start is None:
        return None, None
    last = start
    for j in range(start + 1, len(lines)):
        t = lines[j].strip()
        if t.startswith("["):
            break
        if t and not t.startswith("#"):
            last = j
    return start, last


def migrate(text):
    nl = "\r\n" if "\r\n" in text else "\n"
    lines = text.split(nl)
    out = list(lines)

    # schema_version
    for i, ln in enumerate(out):
        if re.match(r"\s*schema_version\s*=", ln):
            out[i] = "schema_version = 7"
            break
    else:
        raise ValueError("schema_version 없음")

    # [house] drip_installed
    s, e = section_bounds(out, "[house]")
    if s is None:
        raise ValueError("[house] 없음")
    body = out[s:e + 1]
    if not any(re.match(r"\s*drip_installed\s*=", b) for b in body):
        out.insert(e + 1, "drip_installed = true  # M5b: 점적 설치 (house-humidity)")

    # [microbes] anthracnose
    s, e = section_bounds(out, "[microbes]")
    if s is None:
        raise ValueError("[microbes] 없음")
    body = out[s:e + 1]
    if not any(re.match(r"\s*anthracnose\s*=", b) for b in body):
        out.insert(e + 1, "anthracnose = 0.0  # M5b: 탄저병균 시작 지수 (foliar-disease 8장)")

    # [foliar]
    s, _ = section_bounds(out, "[foliar]")
    if s is None:
        # [microbes] 섹션 끝(다음 헤더 앞의 빈 줄 포함)에 이어서 넣어요
        s, e = section_bounds(out, "[microbes]")
        block = [
            "",
            "[foliar]  # M5b 필수: 바깥에서 늘 들어오는 포자 수준 (foliar-disease 8장). 0 = 배경 포자 없음",
            "botrytis_background_index = 0.0",
            "powdery_mildew_background_index = 0.0",
        ]
        out[e + 1:e + 1] = block
    return nl.join(out)


def main():
    root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "data/scenarios")
    changed = 0
    for p in sorted(root.rglob("*.toml")):
        old = p.read_bytes().decode("utf-8")
        if old.startswith("﻿"):
            raise ValueError(f"BOM: {p}")
        new = migrate(old)
        if new != old:
            p.write_bytes(new.encode("utf-8"))
            changed += 1
    print(f"변경 {changed}개")


if __name__ == "__main__":
    main()
