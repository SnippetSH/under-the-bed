# 행동 이름 (플레이어 말). 취소 알림 등에 써요 (M6b P12)
extends RefCounted

const RejectText = preload("res://scripts/reject_text.gd")

const NAMES := {
	"irrigate": "물 주기", "add_fertilizer": "비료 주기", "add_compost": "퇴비 넣기", "add_lime": "석회 넣기",
	"till": "땅 갈기", "set_mulch": "멀칭", "apply_disinfectant": "소독", "add_inoculant": "유익균 넣기",
	"thin_flowers": "꽃 솎기", "remove_plant": "포기 뽑기", "release_predators": "천적 풀기",
	"set_window": "측창 조작", "set_heater": "난방기 조작", "set_fan": "유동팬 조작",
	"set_cover_layers": "비닐 겹수 바꾸기", "install_drip": "점적 호스 깔기", "plant": "정식(모종 심기)",
}


static func of(action: String) -> String:
	return NAMES.get(action, "행동")


# "물 주기를" / "소독을": 받침에 따라 을·를
static func with_object(name: String) -> String:
	var last := name
	if name.ends_with(")"):
		last = name.substr(0, name.find("("))
	return name + ("을" if RejectText._has_batchim(last) else "를")
