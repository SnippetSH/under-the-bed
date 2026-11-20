# 도구 줄 (M6b 명세 5.1): 도구 11종 → 1 × 1 영역 명령. 게임 쪽 표예요(코어에는 명령 id만 있어요).
#  kind_key: 종류를 고르는 명령 키(카탈로그 cat), amount_key: 양 키, def: game_config [inputs]의 입력칸 이름(기본값·범위·간격).
extends RefCounted

const Cfg = preload("res://scripts/cfg.gd")

# 숫자 키 순서 (1~9, 0, -)
const TOOLS := [
	{"id": "watering_can", "label": "물뿌리개", "action": "irrigate", "amount_key": "amount_mm", "unit": "mm", "def": "irrigate_mm", "marker": "#5aa9ff"},
	{"id": "fertilizer", "label": "비료", "action": "add_fertilizer", "kind_key": "fertilizer", "cat": "fertilizer", "kind_def": "calcium_nitrate", "amount_key": "g_per_m2", "unit": "g/m²", "def": "fertilizer_g_per_m2", "marker": "#f2f2f2"},
	{"id": "compost", "label": "퇴비", "action": "add_compost", "kind_key": "compost", "cat": "compost", "kind_def": "mature_manure", "amount_key": "kg_per_m2", "unit": "kg/m²", "def": "compost_kg_per_m2", "marker": "#5b3a1e"},
	{"id": "lime", "label": "석회", "action": "add_lime", "kind_key": "lime", "cat": "lime", "kind_def": "calcium_carbonate", "amount_key": "g_per_m2", "unit": "g/m²", "def": "lime_g_per_m2", "marker": "#e8e4d0"},
	{"id": "hoe", "label": "괭이(경운)", "action": "till", "marker": "#a07040"},
	{"id": "mulch", "label": "멀칭 필름", "action": "set_mulch", "kind_key": "mulch", "cat": "mulch", "kind_def": "black_film", "marker": "#303030"},
	{"id": "disinfectant", "label": "소독제", "action": "apply_disinfectant", "kind_key": "disinfectant", "cat": "disinfectant", "kind_def": "dazomet", "amount_key": "g_per_m2", "unit": "g/m²", "def": "disinfectant_g_per_m2", "marker": "#c8e04a"},
	{"id": "inoculant", "label": "유익균", "action": "add_inoculant", "kind_key": "inoculant", "cat": "inoculant", "kind_def": "trichoderma_product", "amount_key": "g_per_m2", "unit": "g/m²", "def": "inoculant_g_per_m2", "marker": "#9be0a0"},
	{"id": "scissors", "label": "가위(적화·적과)", "action": "thin_flowers", "amount_key": "per_plant", "unit": "개/포기", "def": "thin_per_plant", "marker": "#ff9ad0"},
	{"id": "pull", "label": "뽑기", "action": "remove_plant", "marker": "#ff5040"},
	{"id": "predators", "label": "천적 통", "action": "release_predators", "kind_key": "predator", "cat": "predator", "kind_def": "persimilis", "amount_key": "per_m2", "unit": "마리/m²", "def": "predators_per_m2", "marker": "#ffb000"},
]


# 도구별 처음 설정 {kind, amount}. 카탈로그에 기본 종류가 없으면 첫 항목.
static func default_settings(catalog: Dictionary) -> Array:
	var out := []
	for t in TOOLS:
		var s := {}
		if t.has("kind_key"):
			var ids: Array = Array(catalog.get(t["cat"], PackedStringArray()))
			s["kind"] = t["kind_def"] if ids.has(t["kind_def"]) else (str(ids[0]) if not ids.is_empty() else "")
		if t.has("amount_key"):
			s["amount"] = float(Cfg.input(t["def"])["def"])
		out.append(s)
	return out


static func build_command(tool_index: int, settings: Dictionary, cell_index: int, cols: int) -> Dictionary:
	var t: Dictionary = TOOLS[tool_index]
	var cmd := {"action": t["action"], "area": [cell_index / cols, cell_index % cols, 1, 1]}
	if t.has("kind_key"):
		cmd[t["kind_key"]] = str(settings["kind"])
	if t.has("amount_key"):
		cmd[t["amount_key"]] = float(settings["amount"])
	return cmd


static func describe(tool_index: int, settings: Dictionary, name_of: Callable) -> String:
	var t: Dictionary = TOOLS[tool_index]
	var parts := PackedStringArray([t["label"]])
	if t.has("kind_key"):
		parts.push_back(name_of.call(str(settings["kind"])))
	if t.has("amount_key"):
		parts.push_back("%s %s" % [str(snappedf(float(settings["amount"]), 0.01)), t["unit"]])
	return " · ".join(parts)
