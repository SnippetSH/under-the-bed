# P3 / E3 축소판: 일정표가 있는 season_standard를 k틱 돌리고(플레이어 명령 몇 개 + 저장 직전 대기 명령 1개),
# 코어 바이트 + 엔진 상태를 JSON 묶음 파일로 저장 → 새 SimWorld로 불러와 이어서 N틱까지.
# 끊지 않고 돌린 실행과 모든 틱 해시, 명령 기록이 같아야 하고, 불러온 직후 다시 저장한 코어 바이트·엔진 상태도 같아야 해요.
#   godot --headless --path game --script res://tests/p3_save.gd -- out=<폴더> [k=1000] [ticks=2000]
extends SceneTree

const U = preload("res://tests/util.gd")
const SCENARIO := "res://data/scenarios/m4/season_standard.toml"

var player := {
	50: [{"action": "set_fan", "on": true}],
	500: [{"action": "add_fertilizer", "area": [0, 0, 2, 20], "fertilizer": "urea", "g_per_m2": 2.0}],
	900: [{"action": "set_heater", "on": true, "setpoint_degC": 6.0}],
	1000: [{"action": "irrigate", "area": [0, 0, 1, 1], "amount_mm": 1.0}],  # k = 1000이면 저장 때 대기 중
	1500: [{"action": "add_fertilizer", "fertilizer": "potassium_sulfate", "g_per_m2": 3.0}],
}


func enqueue_player(w: Object, t: int) -> bool:
	for c in player.get(t, []):
		var r: Dictionary = w.enqueue(c)
		if not r["ok"]:
			printerr("enqueue failed at %d: %s" % [t, r])
			return false
	return true


# 틱 from..to-1을 돌려요. 각 틱 직전에 플레이어 명령 (skip_first면 from 틱 명령은 이미 넣은 것으로 봐요)
func run(w: Object, from_tick: int, to_tick: int, hashes: PackedStringArray, skip_first: bool) -> bool:
	for t in range(from_tick, to_tick):
		if not (skip_first and t == from_tick) and not enqueue_player(w, t):
			return false
		var a: Dictionary = w.advance(1)
		if not a["ok"]:
			printerr("advance failed: %s" % a)
			return false
		hashes.push_back(w.state_hash())
	return true


func _init() -> void:
	var args := U.parse_args()
	var k := int(args.get("k", "1000"))
	var n := int(args.get("ticks", "2000"))

	# 끊지 않은 실행
	var full: Object = U.new_world(SCENARIO, true)
	var full_hashes := PackedStringArray([full.state_hash()])
	if not run(full, 0, n, full_hashes, false):
		quit(1)
		return

	# k틱 + 저장
	var a: Object = U.new_world(SCENARIO, true)
	var resumed := PackedStringArray([a.state_hash()])
	if not run(a, 0, k, resumed, false) or not enqueue_player(a, k):  # 틱 k의 플레이어 명령까지 넣고(대기 중) 저장
		quit(1)
		return
	var pending_before: int = a.get_pending_command_count()
	var core: PackedByteArray = a.save_bytes()
	var engine: Dictionary = a.get_engine_state()
	var engine_json := JSON.stringify(engine)
	var bundle := JSON.stringify({"bundle_format": 1, "engine": engine, "core_base64": Marshalls.raw_to_base64(core)})
	var path: String = args["out"].path_join("p3_save.json")
	if not U.write_text(path, bundle):
		quit(1)
		return
	print("saved tick=%d pending=%d core_bytes=%d bundle_chars=%d hash=%s" % [a.get_tick(), pending_before, core.size(), bundle.length(), a.state_hash()])

	# 새 SimWorld로 불러오기 (JSON을 거쳐 정수가 실수가 된 엔진 상태)
	var parsed: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(path))
	var b: Object = ClassDB.instantiate("SimWorld")
	var r: Dictionary = b.load_bytes(Marshalls.base64_to_raw(parsed["core_base64"]), parsed["engine"])
	if not r["ok"]:
		printerr("load_bytes failed: %s" % r)
		quit(1)
		return
	var ok := true
	if b.save_bytes() != core:
		printerr("re-saved core bytes differ")
		ok = false
	if JSON.stringify(b.get_engine_state()) != engine_json:
		printerr("re-saved engine state differs")
		ok = false
	if b.get_tick() != k or b.get_pending_command_count() != pending_before or b.state_hash() != a.state_hash():
		printerr("loaded tick/pending/hash differ: %d %d %s" % [b.get_tick(), b.get_pending_command_count(), b.state_hash()])
		ok = false
	if not run(b, k, n, resumed, true):
		quit(1)
		return
	var diff := U.compare(full_hashes, resumed)
	print("save_load k=%d n=%d rows=%d %s final=%s" % [k, n, resumed.size(), "MATCH" if diff == "" else diff, resumed[-1]])
	var log_full: String = ClassDB.class_call_static("SimWorld", "format_schedule_toml", full.get_command_log())
	var log_resumed: String = ClassDB.class_call_static("SimWorld", "format_schedule_toml", b.get_command_log())
	print("command_log entries=%d %s" % [full.get_command_log().size(), "MATCH" if log_full == log_resumed else "DIFF"])
	if diff != "" or log_full != log_resumed:
		ok = false
	# 엔진 상태 없이 불러오면 일정표가 없어요 (묶음에서 따로 복원해야 함을 확인)
	var c: Object = ClassDB.instantiate("SimWorld")
	var rc: Dictionary = c.load_bytes(core)
	print("load_without_engine_state ok=%s schedule_after=%d" % [rc["ok"], c.get_engine_state()["schedule_toml"].length()])
	quit(0 if ok else 1)
