# 한국어 이름표와 행동 입력 양식 (게임 쪽 표, 코어에는 id만 있어요). 표에 없는 id는 id 그대로 보여줘요.
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


static func name_of(id: String) -> String:
	return NAMES.get(id, id)


# 행동 양식. area: 영역을 쓰는지. fields: 입력칸 [{key, kind, label, ...}]
#  kind: "num"(실수), "int", "bool", "cat"(카탈로그 목록, cat=키), "levels"(창 개방 단계), "layer"(피복 겹), "side"
static func actions() -> Array:
	var d := func(k: String) -> float: return float(Cfg.get_v("defaults", k))
	return [
		{"id": "irrigate", "label": "물 주기", "area": true, "fields": [
			{"key": "amount_mm", "kind": "num", "label": "양 (mm)", "def": d.call("irrigate_mm"), "min": 0.0, "max": 100.0, "step": 0.5}]},
		{"id": "add_fertilizer", "label": "비료 주기", "area": true, "fields": [
			{"key": "fertilizer", "kind": "cat", "cat": "fertilizer", "label": "종류"},
			{"key": "g_per_m2", "kind": "num", "label": "양 (g/m²)", "def": d.call("fertilizer_g_per_m2"), "min": 0.0, "max": 500.0, "step": 0.5}]},
		{"id": "plant", "label": "정식", "area": false, "fields": [
			{"key": "seedling_grade", "kind": "cat", "cat": "seedling_grade", "label": "모종 등급", "def_id": "B"},
			{"key": "plants_per_m2", "kind": "num", "label": "포기/m²", "def": d.call("plants_per_m2"), "min": 1.0, "max": 20.0, "step": 1.0}]},
		{"id": "set_window", "label": "측창 여닫기", "area": false, "fields": [
			{"key": "layer", "kind": "layer", "label": "겹 (0 = 가장 바깥)"},
			{"key": "side", "kind": "side", "label": "좌·우"},
			{"key": "open_fraction", "kind": "levels", "label": "개방 단계"}]},
		{"id": "set_heater", "label": "난방기", "area": false, "fields": [
			{"key": "on", "kind": "bool", "label": "켜기", "def": true},
			{"key": "setpoint_degC", "kind": "num", "label": "설정 온도 (°C)", "def": d.call("heater_setpoint_degC"), "min": 0.0, "max": 30.0, "step": 1.0}]},
		{"id": "set_fan", "label": "유동팬", "area": false, "fields": [
			{"key": "on", "kind": "bool", "label": "켜기", "def": true}]},
		{"id": "set_cover_layers", "label": "피복 겹수 바꾸기", "area": false, "fields": [
			{"key": "layers", "kind": "int", "label": "겹수", "def": 3, "min": 2, "max": 4}]},
		{"id": "install_drip", "label": "점적 호스 깔기", "area": false, "fields": []},
		{"id": "add_compost", "label": "퇴비 넣기", "area": true, "fields": [
			{"key": "compost", "kind": "cat", "cat": "compost", "label": "종류"},
			{"key": "kg_per_m2", "kind": "num", "label": "양 (kg/m²)", "def": d.call("compost_kg_per_m2"), "min": 0.0, "max": 20.0, "step": 0.5}]},
		{"id": "add_lime", "label": "석회 넣기", "area": true, "fields": [
			{"key": "lime", "kind": "cat", "cat": "lime", "label": "종류"},
			{"key": "g_per_m2", "kind": "num", "label": "양 (g/m²)", "def": d.call("lime_g_per_m2"), "min": 0.0, "max": 1000.0, "step": 10.0}]},
		{"id": "till", "label": "경운", "area": true, "fields": []},
		{"id": "set_mulch", "label": "멀칭 덮기·걷기", "area": true, "fields": [
			{"key": "mulch", "kind": "cat", "cat": "mulch", "label": "종류"}]},
		{"id": "apply_disinfectant", "label": "약제 소독", "area": true, "fields": [
			{"key": "disinfectant", "kind": "cat", "cat": "disinfectant", "label": "종류"},
			{"key": "g_per_m2", "kind": "num", "label": "양 (g/m²)", "def": d.call("disinfectant_g_per_m2"), "min": 0.0, "max": 200.0, "step": 1.0}]},
		{"id": "add_inoculant", "label": "유익균 넣기", "area": true, "fields": [
			{"key": "inoculant", "kind": "cat", "cat": "inoculant", "label": "종류"},
			{"key": "g_per_m2", "kind": "num", "label": "양 (g/m²)", "def": d.call("inoculant_g_per_m2"), "min": 0.0, "max": 100.0, "step": 1.0}]},
		{"id": "thin_flowers", "label": "적화·적과", "area": true, "fields": [
			{"key": "per_plant", "kind": "num", "label": "포기당 개수", "def": d.call("thin_per_plant"), "min": 0.0, "max": 30.0, "step": 1.0}]},
		{"id": "remove_plant", "label": "병든 포기 뽑기", "area": true, "fields": []},
		{"id": "release_predators", "label": "천적 방사", "area": true, "fields": [
			{"key": "predator", "kind": "cat", "cat": "predator", "label": "종류"},
			{"key": "per_m2", "kind": "num", "label": "마리/m²", "def": d.call("predators_per_m2"), "min": 0.0, "max": 50.0, "step": 1.0}]},
	]
