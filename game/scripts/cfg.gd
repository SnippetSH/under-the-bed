# 게임 쪽 설정 읽기 (config/game_config.cfg). 전부 게임용 임의값이에요 (시뮬레이션 파라미터가 아니에요).
extends RefCounted

const PATH := "res://config/game_config.cfg"
static var _cf: ConfigFile = null


static func _load() -> ConfigFile:
	if _cf == null:
		_cf = ConfigFile.new()
		var e := _cf.load(PATH)
		if e != OK:
			push_error("설정 파일을 읽지 못했어요: %s (%d)" % [PATH, e])
	return _cf


static func get_v(section: String, key: String) -> Variant:
	return _load().get_value(section, key)


static func color(key: String) -> Color:
	return Color(str(_load().get_value("colors", key)))


# [inputs]의 [기본값, 최소, 최대, 간격] -> {def, min, max, step}
static func input(key: String) -> Dictionary:
	var a: Array = _load().get_value("inputs", key)
	return {"def": float(a[0]), "min": float(a[1]), "max": float(a[2]), "step": float(a[3])}


# 시작 화면 순서대로의 시나리오 id 목록
static func scenario_ids() -> Array:
	return Array(_load().get_value("start", "scenario_order"))


# 시나리오 항목 {id, label, description, path, season_ticks, mode, auto_irrigation}. 없으면 빈 사전.
static func scenario(id: String) -> Dictionary:
	var section := "scenario_" + id
	var cf := _load()
	if not cf.has_section(section):
		return {}
	var d := {"id": id}
	for k in ["label", "description", "path", "season_ticks", "mode", "auto_irrigation"]:
		d[k] = cf.get_value(section, k)
	d["season_ticks"] = int(d["season_ticks"])
	return d
