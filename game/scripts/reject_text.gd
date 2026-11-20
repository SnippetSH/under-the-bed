# 거부 알림을 플레이어 말로 (M6b P8). 코어 메시지(영어 원문)는 화면에 보이지 않고 개발자 패널(F3) 기록에만 가요.
# 명령 종류 + 원문의 알려진 꼴로 상황을 고르고, 날짜·범위는 get_catalog() 값에서 만들어요(하드코딩 안 해요).
# 모르는 경우는 일반 문장으로 떨어져요. 이 빌드에는 can_enqueue가 없어서 거부 원문에 맞춰 골라요.
extends RefCounted

const MONTH_DAYS := [31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31]   # 윤년 없는 달력 (코어와 같아요)
const FALLBACK := "지금은 할 수 없는 행동이에요."

# 양이 0 이상이어야 하는 도구: 행동 → 무엇의 양인지
const AMOUNT_NAMES := {
	"irrigate": "물 주는 양", "add_fertilizer": "비료 양", "add_compost": "퇴비 양", "add_lime": "석회 양",
	"apply_disinfectant": "소독제 양", "add_inoculant": "유익균 양", "thin_flowers": "포기마다 남길 꽃 수",
	"release_predators": "천적 수",
}


static func doy_to_text(doy: int) -> String:
	var d := doy
	var m := 0
	while m < 11 and d > MONTH_DAYS[m]:
		d -= MONTH_DAYS[m]
		m += 1
	return "%d월 %d일" % [m + 1, d]


# cmd: 넣으려던 명령(없으면 빈 사전), raw: 코어·러너 원문, catalog: get_catalog(), hc: get_house_controls()
static func korean(cmd: Dictionary, raw: String, catalog: Dictionary, hc: Dictionary) -> String:
	if raw.begins_with("자동 물 주기"):
		return "자동 물 주기를 하지 못했어요. 출입구 쪽 조절기에서 양을 확인해 주세요."
	if raw.begins_with("시뮬레이션 오류"):
		return "문제가 생겨서 시간을 멈췄어요."
	var action := str(cmd.get("action", ""))
	match action:
		"plant":
			if raw.contains("live crop"):
				return "밭에 포기가 자라고 있어서 지금은 새로 심을 수 없어요."
			if raw.contains("plants_per_m2"):
				return "심을 포기 수는 0보다 많아야 해요."
			if raw.contains("seedling"):
				return "그런 모종은 없어요."
		"set_cover_layers":
			if raw.contains("day_of_year"):
				var w: Array = Array(catalog.get("cover_change_window_doy", []))
				if w.size() == 2:
					return "지금은 비닐 겹수를 바꿀 수 없어요. %s부터 %s 사이에만 바꿀 수 있어요." % [doy_to_text(int(w[0])), doy_to_text(int(w[1]))]
				return "지금은 비닐 겹수를 바꿀 수 없는 때예요."
			if raw.contains("layers must be"):
				return "비닐은 %d겹에서 %d겹 사이로만 씌울 수 있어요." % [int(catalog.get("min_cover_layers", 0)), int(catalog.get("max_cover_layers", 0))]
		"install_drip":
			if raw.contains("already"):
				return "점적 호스는 이미 깔려 있어요."
		"set_window":
			if raw.contains("outside"):
				return "그 겹의 측창은 없어요. 지금 비닐은 %d겹이에요." % int(hc.get("cover_layers", 0))
			if raw.contains("allowed level"):
				return "측창은 정해진 단계로만 열 수 있어요."
		"set_heater":
			if raw.contains("setpoint"):
				return "난방 온도는 %d도에서 %d도 사이로만 맞출 수 있어요." % [int(catalog.get("heater_setpoint_min_degC", 0)), int(catalog.get("heater_setpoint_max_degC", 0))]
		"remove_plant":
			if raw.contains("no crop"):
				return "이 칸에는 뽑을 포기가 없어요."
	if AMOUNT_NAMES.has(action) and raw.contains(">= 0"):
		return "%s은 0보다 적을 수 없어요." % AMOUNT_NAMES[action] if _has_batchim(AMOUNT_NAMES[action]) else "%s는 0보다 적을 수 없어요." % AMOUNT_NAMES[action]
	if raw.contains("area") and (raw.contains("outside") or raw.contains("grid")):
		return "그 자리에는 할 수 없어요."
	return FALLBACK


# 마지막 글자에 받침이 있는지 (은/는 고르기)
static func _has_batchim(word: String) -> bool:
	if word.is_empty():
		return false
	var c := word.unicode_at(word.length() - 1)
	if c < 0xAC00 or c > 0xD7A3:
		return false
	return (c - 0xAC00) % 28 != 0
