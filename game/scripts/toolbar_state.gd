# 도구 줄 보이기 상태 (M6b P10). 그리기 없이 상태만: 헤드리스 검사가 그대로 써요.
#  - pinned(Tab으로 고정해 보이기) / 숨김. 숨김일 때 도구 단축키를 누르면 잠깐 올라왔다(peek) 내려가요.
#  - shown_t: 0(완전히 숨김) ~ 1(완전히 보임). slide_s 동안 움직여요.
extends RefCounted

var pinned := false
var peek_left := 0.0
var shown_t := 0.0
var slide_s := 0.2
var peek_s := 1.2


func setup(default_shown: bool, slide_seconds: float, peek_seconds: float) -> void:
	pinned = default_shown
	slide_s = maxf(0.001, slide_seconds)
	peek_s = peek_seconds
	peek_left = 0.0
	shown_t = 1.0 if pinned else 0.0


func target_shown() -> bool:
	return pinned or peek_left > 0.0


# Tab: 고정해 보이기 ↔ 숨기기
func toggle() -> void:
	pinned = not pinned
	peek_left = 0.0


# 숨김일 때 도구 단축키: 잠깐 보여 주기 (진행 중이면 시간을 처음부터)
func peek() -> void:
	if not pinned:
		peek_left = peek_s


func set_pinned(on: bool, instant := true) -> void:
	pinned = on
	peek_left = 0.0
	if instant:
		shown_t = 1.0 if on else 0.0


func update(dt: float) -> void:
	if peek_left > 0.0:
		peek_left = maxf(0.0, peek_left - dt)
	var goal := 1.0 if target_shown() else 0.0
	shown_t = move_toward(shown_t, goal, dt / slide_s)


# 화면 위치용 (부드럽게)
func eased() -> float:
	return smoothstep(0.0, 1.0, shown_t)


# 마우스로 칸을 누를 수 있는지 (조금이라도 보이면)
func clickable() -> bool:
	return shown_t > 0.5
