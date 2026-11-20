# 게임 쪽 설정 읽기 (config/game_config.cfg). 전부 게임용 임의값.
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
