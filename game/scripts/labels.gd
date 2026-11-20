# 한국어 이름표와 행동 입력 양식 (게임 쪽 표, 코어에는 id만 있어요). 표에 없는 id는 id 그대로 보여줘요.
# 입력칸의 기본값·범위는 game_config.cfg [inputs], 선택지·난방 범위·겹수 범위는 카탈로그(get_catalog)에서 와요.
extends RefCounted

const Cfg = preload("res://scripts/cfg.gd")

const NAMES := {
	"immature_manure": "덜 익은 퇴비", "mature_manure": "완숙 퇴비", "bark_compost": "바크 퇴비",
	"ammonium_sulfate": "황산암모늄", "urea": "요소", "calcium_nitrate": "질산칼슘", "potassium_sulfate": "황산칼륨",
	"superphosphate": "과인산석회", "magnesium_sulfate": "황산마그네슘", "manganese_sulfate": "황산망간",
	"calcium_carbonate": "탄산칼슘(석회)", "dolomite": "고토석회",
	"clear_film": "투명 필름", "black_film": "흑색 필름", "white_film": "백색 필름", "none": "없음(걷기)",
	"dazomet": "다조멧", "chloropicrin": "클로로피크린",
	"bacillus_product": "바실러스 제품", "trichoderma_product": "트리코더마 제품", "mycorrhiza_product": "균근균 제품",
	"persimilis": "칠레이리응애", "left": "왼쪽", "right": "오른쪽",
}

# 코어가 거부했을 때 보여 주는 한국어 한 줄 (B8: 코어의 영어 메시지는 이 줄 뒤에 붙여요). 행동 id별 대표 이유.
const REJECT_SUMMARY := {
	"plant": "작물이 살아 있는 동안은 정식할 수 없어요.",
	"set_cover_layers": "피복 겹수는 휴작기에만 바꿀 수 있어요.",
	"install_drip": "점적 호스는 한 번만 깔 수 있어요.",
	"remove_plant": "뽑을 포기가 없는 영역이에요.",
}
const REJECT_DEFAULT := "지금은 할 수 없는 행동이에요."


static func name_of(id: String) -> String:
	return NAMES.get(id, id)


static func reject_summary(action_id: String) -> String:
	return REJECT_SUMMARY.get(action_id, REJECT_DEFAULT)


# 행동 17종 (m6-overview 6.2). area: 영역을 쓰는지. fields: 입력칸 [{key, kind, label, ...}]
#  kind: "num"(실수), "int", "bool", "cat"(카탈로그 목록, cat=키), "levels"(창 개방 단계), "layer"(피복 겹), "side"
# 숫자칸의 def/min/max/step은 설정 파일([inputs])과 카탈로그에서 채워요.
static func actions(cat: Dictionary) -> Array:
	var n := func(key: String, label: String, field_key: String) -> Dictionary:
		var r := Cfg.input(key)
		return {"key": field_key, "kind": "num", "label": label, "def": r["def"], "min": r["min"], "max": r["max"], "step": r["step"]}
	var heater := Cfg.input("heater_setpoint_degC")
	var cover := Cfg.input("cover_layers")
	var min_layers := float(cat.get("min_cover_layers", cover["min"]))
	var max_layers := float(cat.get("max_cover_layers", cover["max"]))
	return [
		{"id": "irrigate", "label": "물 주기", "area": true, "fields": [n.call("irrigate_mm", "양 (mm)", "amount_mm")]},
		{"id": "add_fertilizer", "label": "비료 주기", "area": true, "fields": [
			{"key": "fertilizer", "kind": "cat", "cat": "fertilizer", "label": "종류"},
			n.call("fertilizer_g_per_m2", "양 (g/m²)", "g_per_m2")]},
		{"id": "plant", "label": "정식", "area": false, "fields": [
			{"key": "seedling_grade", "kind": "cat", "cat": "seedling_grade", "label": "모종 등급", "def_id": "B"},
			n.call("plants_per_m2", "포기/m²", "plants_per_m2")]},
		{"id": "set_window", "label": "측창 여닫기", "area": false, "fields": [
			{"key": "layer", "kind": "layer", "label": "겹 (0 = 가장 바깥)"},
			{"key": "side", "kind": "side", "label": "좌·우"},
			{"key": "open_fraction", "kind": "levels", "label": "개방 단계"}]},
		{"id": "set_heater", "label": "난방기", "area": false, "fields": [
			{"key": "on", "kind": "bool", "label": "켜기", "def": true},
			{"key": "setpoint_degC", "kind": "num", "label": "설정 온도 (°C)", "def": heater["def"],
				"min": float(cat.get("heater_setpoint_min_degC", heater["min"])),
				"max": float(cat.get("heater_setpoint_max_degC", heater["max"])), "step": heater["step"]}]},
		{"id": "set_fan", "label": "유동팬", "area": false, "fields": [
			{"key": "on", "kind": "bool", "label": "켜기", "def": true}]},
		{"id": "set_cover_layers", "label": "피복 겹수 바꾸기", "area": false, "fields": [
			{"key": "layers", "kind": "int", "label": "겹수", "def": clampf(cover["def"], min_layers, max_layers),
				"min": min_layers, "max": max_layers, "step": cover["step"]}]},
		{"id": "install_drip", "label": "점적 호스 깔기", "area": false, "fields": []},
		{"id": "add_compost", "label": "퇴비 넣기", "area": true, "fields": [
			{"key": "compost", "kind": "cat", "cat": "compost", "label": "종류"},
			n.call("compost_kg_per_m2", "양 (kg/m²)", "kg_per_m2")]},
		{"id": "add_lime", "label": "석회 넣기", "area": true, "fields": [
			{"key": "lime", "kind": "cat", "cat": "lime", "label": "종류"},
			n.call("lime_g_per_m2", "양 (g/m²)", "g_per_m2")]},
		{"id": "till", "label": "경운", "area": true, "fields": []},
		{"id": "set_mulch", "label": "멀칭 덮기·걷기", "area": true, "fields": [
			{"key": "mulch", "kind": "cat", "cat": "mulch", "label": "종류"}]},
		{"id": "apply_disinfectant", "label": "약제 소독", "area": true, "fields": [
			{"key": "disinfectant", "kind": "cat", "cat": "disinfectant", "label": "종류"},
			n.call("disinfectant_g_per_m2", "양 (g/m²)", "g_per_m2")]},
		{"id": "add_inoculant", "label": "유익균 넣기", "area": true, "fields": [
			{"key": "inoculant", "kind": "cat", "cat": "inoculant", "label": "종류"},
			n.call("inoculant_g_per_m2", "양 (g/m²)", "g_per_m2")]},
		{"id": "thin_flowers", "label": "적화·적과", "area": true, "fields": [
			n.call("thin_per_plant", "포기당 개수", "per_plant")]},
		{"id": "remove_plant", "label": "병든 포기 뽑기", "area": true, "fields": []},
		{"id": "release_predators", "label": "천적 방사", "area": true, "fields": [
			{"key": "predator", "kind": "cat", "cat": "predator", "label": "종류"},
			n.call("predators_per_m2", "마리/m²", "per_m2")]},
	]


# 입력칸을 만들지 않고 기본값만으로 만든 명령 (행동 단추의 "지금 할 수 있나" 미리 보기용, can_enqueue에 넣어요).
# area는 영역 행동에만 붙어요.
static func default_command(a: Dictionary, cat: Dictionary, area: Array) -> Dictionary:
	var cmd := {"action": a["id"]}
	if a["area"]:
		cmd["area"] = area.duplicate()
	for f in a["fields"]:
		match f["kind"]:
			"num":
				cmd[f["key"]] = float(f["def"])
			"int":
				cmd[f["key"]] = int(f["def"])
			"bool":
				cmd[f["key"]] = bool(f.get("def", true))
			"cat":
				var ids: Array = Array(cat.get(f["cat"], PackedStringArray()))
				var pick: String = f.get("def_id", "")
				cmd[f["key"]] = pick if pick in ids else (str(ids[0]) if not ids.is_empty() else "")
			"side":
				cmd[f["key"]] = "left"
			"layer":
				cmd[f["key"]] = 0
			"levels":
				var lv: PackedFloat64Array = cat.get("window_open_levels", PackedFloat64Array())
				cmd[f["key"]] = float(lv[0]) if lv.size() > 0 else 0.0
	return cmd
