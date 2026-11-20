# 시작 화면 → 본 화면으로 넘기는 선택값 (정적 변수)
extends RefCounted

const MODE_DIRECT := "direct"      # 직접 플레이 (season_standard, 일정표 끔)
const MODE_EMPTY := "empty"        # 직접 플레이, 빈 밭 (empty.toml, 일정표 끔) — 정식부터 해 볼 수 있어요
const MODE_WATCH := "watch"        # 자동 처방 관전 (일정표 켬)
const MODE_LOAD := "load"          # 빠른 저장 불러오기

static var mode: String = MODE_DIRECT
