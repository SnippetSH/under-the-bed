# E3 (m6-overview 10.1): 엔진 경유 세이브 왕복.
# 일정표가 있는 season_standard를 k틱 돌리고(플레이어 명령 몇 개 + 저장 직전 대기 명령 1개),
# 코어 바이트 + get_engine_state()를 JSON 파일로 저장 → 새 SimWorld로 불러와 이어서 n틱까지.
# 끊지 않고 돌린 실행과 모든 틱 해시·명령 기록이 같고, 불러온 직후 다시 저장한 코어 바이트·엔진 상태도 같아야 해요.
# (세이브 묶음 파일 형식(머리글·요약·구역)은 게임 스크립트 몫이라 여기서는 JSON 한 덩어리로 대신해요.)
#   godot --headless --path game --script res://tests/save_roundtrip.gd -- out=<폴더> [k=1000] [ticks=2000]
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


func enqueue_player(w: SimWorld, t: int) -> bool:
	for c in player.get(t, []):
		var r: Dictionary = w.enqueue(c)
		if not r["ok"]:
			printerr("enqueue failed at %d: %s" % [t, r])
			return false
	return true


# 틱 from..to-1을 돌려요. 각 틱 직전에 플레이어 명령 (skip_first면 from 틱 명령은 이미 넣은 것으로 봐요)
func run(w: SimWorld, from_tick: int, to_tick: int, hashes: PackedStringArray, skip_first: bool) -> bool:
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
	var fails := PackedStringArray()

	# 끊지 않은 실행
	var full: SimWorld = U.new_world(args["params"], SCENARIO, true)
	var full_hashes := PackedStringArray([full.state_hash()])
	if not run(full, 0, n, full_hashes, false):
		fails.push_back("uninterrupted run failed")
		U.finish(self, fails)
		return

	# k틱 + 저장 (틱 k의 플레이어 명령까지 넣어 대기 중인 채로)
	var a: SimWorld = U.new_world(args["params"], SCENARIO, true)
	var resumed := PackedStringArray([a.state_hash()])
	if not run(a, 0, k, resumed, false) or not enqueue_player(a, k):
		fails.push_back("first part failed")
		U.finish(self, fails)
		return
	var pending_before: int = a.get_pending_command_count()
	var core: PackedByteArray = a.save_bytes()
	var engine: Dictionary = a.get_engine_state()
	var engine_json := JSON.stringify(engine)
	var bundle := JSON.stringify({"engine": engine, "core_base64": Marshalls.raw_to_base64(core)})
	var path: String = args["out"].path_join("save_roundtrip.json")
	if not U.write_text(path, bundle):
		fails.push_back("cannot write %s" % path)
		U.finish(self, fails)
		return
	print("saved tick=%d pending=%d core_bytes=%d hash=%s" % [a.get_tick(), pending_before, core.size(), a.state_hash()])

	# 새 SimWorld로 불러오기 (JSON을 거쳐 정수가 실수가 된 엔진 상태)
	var parsed: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(path))
	var raw: PackedByteArray = Marshalls.base64_to_raw(parsed["core_base64"])
	var peek: Dictionary = SimWorld.peek_save_version(raw)
	if not peek["ok"] or not peek["supported"] or peek["version"] != SimWorld.core_save_version():
		fails.push_back("peek_save_version on a fresh save: %s" % peek)
	var b := SimWorld.new()
	var r: Dictionary = b.load_bytes(raw, parsed["engine"])
	if not r["ok"]:
		fails.push_back("load_bytes failed: %s" % r)
		U.finish(self, fails)
		return
	if b.save_bytes() != core:
		fails.push_back("re-saved core bytes differ")
	if JSON.stringify(b.get_engine_state()) != engine_json:
		fails.push_back("re-saved engine state differs")
	if b.get_tick() != k or b.get_pending_command_count() != pending_before or b.state_hash() != a.state_hash():
		fails.push_back("loaded tick/pending/hash differ: %d %d %s" % [b.get_tick(), b.get_pending_command_count(), b.state_hash()])
	if not b.is_schedule_enabled():
		fails.push_back("schedule_enabled not restored")
	if not run(b, k, n, resumed, true):
		fails.push_back("resumed run failed")
		U.finish(self, fails)
		return
	var diff := U.compare(full_hashes, resumed)
	print("save_load k=%d n=%d rows=%d %s final=%s" % [k, n, resumed.size(), "MATCH" if diff == "" else diff, resumed[-1]])
	if diff != "":
		fails.push_back("hashes after load: %s" % diff)
	var log_full: String = SimWorld.format_schedule_toml(full.get_command_log())
	var log_resumed: String = SimWorld.format_schedule_toml(b.get_command_log())
	print("command_log entries=%d %s" % [full.get_command_log().size(), "MATCH" if log_full == log_resumed else "DIFF"])
	if log_full != log_resumed:
		fails.push_back("command log differs after load")

	# 엔진 상태 없이 불러오면 일정표·기록이 비어요 (묶음에서 따로 복원해야 함을 확인)
	var c := SimWorld.new()
	var rc: Dictionary = c.load_bytes(core)
	var schedule_after: String = c.get_engine_state()["schedule_toml"]
	print("load_without_engine_state ok=%s schedule_chars=%d" % [rc["ok"], schedule_after.length()])
	if not rc["ok"] or schedule_after.length() != 0 or c.get_command_log().size() != 0:
		fails.push_back("load without engine state should give an empty schedule and log")
	U.finish(self, fails)
