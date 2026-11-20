# E5 (m6-overview 10.1, 7.3): 잘못된 세이브.
# 버전을 바꾼 것, 중간에 자른 것, 빈 것, 매직이 다른 것을 파일로 쓰고 다시 읽어 load_bytes에 넣어요.
# 각각 UnsupportedVersion / Truncated / InvalidData / InvalidData를 받고, 프로세스가 살아 있고,
# 불러오던 월드는 그대로이고, 파일은 바뀌지 않아야 해요. peek_save_version(C1)의 판정도 같이 봐요.
# 엔진 상태 사전이 틀린 경우(형식 번호, 빠진 키, 틱 불일치)도 거부돼야 해요.
#   godot --headless --path game --script res://tests/bad_saves.gd -- out=<폴더>
extends SceneTree

const U = preload("res://tests/util.gd")
const SCENARIO := "res://data/scenarios/m4/season_standard.toml"


func read_bytes(path: String) -> PackedByteArray:
	return FileAccess.get_file_as_bytes(path)


func write_bytes(path: String, bytes: PackedByteArray) -> bool:
	var f := FileAccess.open(path, FileAccess.WRITE)
	if f == null:
		return false
	f.store_buffer(bytes)
	f.close()
	return true


func _init() -> void:
	var args := U.parse_args()
	var fails := PackedStringArray()
	var w: SimWorld = U.new_world(args["params"], SCENARIO, true)
	if w == null:
		fails.push_back("load failed")
		U.finish(self, fails)
		return
	w.advance(100)
	var good := w.save_bytes()
	var engine := w.get_engine_state()

	var other_version := good.duplicate()
	other_version[4] = 7
	var bad_magic := good.duplicate()
	bad_magic[0] = 0x58
	# [이름, 바이트, load_bytes 기대 코드, peek 기대: 읽히는 버전(-1 = 오류 코드 기대), peek 오류 코드]
	var cases := [
		["other_version", other_version, "UnsupportedVersion", 7, ""],
		["truncated", good.slice(0, good.size() / 2), "Truncated", SimWorld.core_save_version(), ""],
		["empty", PackedByteArray(), "InvalidData", -1, "InvalidData"],
		["bad_magic", bad_magic, "InvalidData", -1, "InvalidData"],
	]
	var hash_before := w.state_hash()
	var tick_before := w.get_tick()
	for c in cases:
		var path: String = ProjectSettings.globalize_path(args["out"].path_join("bad_%s.sav" % c[0]))
		if not write_bytes(path, c[1]):
			fails.push_back("cannot write %s" % path)
			continue
		var on_disk := read_bytes(path)
		var peek: Dictionary = SimWorld.peek_save_version(on_disk)
		var r: Dictionary = w.load_bytes(on_disk, engine)
		print("%s bytes=%d load=%s/%s peek=%s" % [c[0], on_disk.size(), r["ok"], r["code"], peek])
		if r["ok"] or r["code"] != c[2] or String(r["message"]) == "":
			fails.push_back("%s: expected %s, got %s" % [c[0], c[2], r])
		if c[3] >= 0:
			if not peek["ok"] or peek["version"] != c[3] or peek["supported"] != (c[3] == SimWorld.core_save_version()):
				fails.push_back("%s: peek expected version %d, got %s" % [c[0], c[3], peek])
		elif peek["ok"] or peek["code"] != c[4] or peek["supported"]:
			fails.push_back("%s: peek expected %s, got %s" % [c[0], c[4], peek])
		if read_bytes(path) != c[1]:
			fails.push_back("%s: file changed" % c[0])
		if w.state_hash() != hash_before or w.get_tick() != tick_before:
			fails.push_back("%s: failed load changed the world" % c[0])

	# 엔진 상태 사전이 틀린 경우 (코어 바이트는 정상)
	var wrong_format := engine.duplicate()
	wrong_format["format"] = 99
	var missing_key := engine.duplicate()
	missing_key.erase("log_toml")
	var wrong_tick := engine.duplicate()
	wrong_tick["tick"] = 5
	var wrong_source := engine.duplicate(true)
	if wrong_source["log_source"].size() > 0:
		wrong_source["log_source"][0] = "robot"
	var engine_cases := [
		["engine_format", wrong_format, "UnsupportedVersion"],
		["engine_missing_key", missing_key, "InvalidData"],
		["engine_tick", wrong_tick, "InvalidData"],
		["engine_source", wrong_source, "InvalidData"],
	]
	for c in engine_cases:
		var r: Dictionary = w.load_bytes(good, c[1])
		print("%s load=%s/%s %s" % [c[0], r["ok"], r["code"], r["message"]])
		if r["ok"] or r["code"] != c[2]:
			fails.push_back("%s: expected %s, got %s" % [c[0], c[2], r])
		if w.state_hash() != hash_before:
			fails.push_back("%s: failed load changed the world" % c[0])

	# 다 거부된 뒤에도 월드는 계속 돌아가요
	var a: Dictionary = w.advance(10)
	if not a["ok"] or w.get_tick() != tick_before + 10:
		fails.push_back("world does not continue after rejected loads")
	U.finish(self, fails)
