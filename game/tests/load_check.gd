# E1 (m6-overview 10.1): 확장이 로드되는가.
# 엔진 버전이 4.7.2인지, SimWorld가 등록됐는지, empty.toml을 읽은 틱 0 해시가 CLI(--ticks 0)의 해시와 같은지 봐요.
# 세이브 버전 접근자(C1)도 같이 확인해요: core_save_version() == peek_save_version(save_bytes()).version.
#   godot --headless --path game --script res://tests/load_check.gd -- cli=<sim-cli> [params=...]
# SimWorld 타입 이름을 직접 쓰지 않아요(확장이 안 올라왔을 때도 스크립트가 읽혀서 이유를 찍게).
extends SceneTree

const U = preload("res://tests/util.gd")
const SCENARIO := "res://data/scenarios/empty.toml"
const EXPECTED_ENGINE := [4, 7, 2]  # ADR-0015: Godot 4.7.2 고정


func _init() -> void:
	var args := U.parse_args()
	var fails := PackedStringArray()
	var info := Engine.get_version_info()
	print("engine=%s" % info["string"])
	if [info["major"], info["minor"], info["patch"]] != EXPECTED_ENGINE:
		fails.push_back("engine version %d.%d.%d is not 4.7.2" % [info["major"], info["minor"], info["patch"]])
	if not ClassDB.class_exists("SimWorld"):
		fails.push_back("SimWorld class not registered")
		U.finish(self, fails)
		return
	var w: Object = ClassDB.instantiate("SimWorld")
	var r: Dictionary = w.load_files(args["params"], SCENARIO)
	if not r["ok"]:
		fails.push_back("load_files: %s %s" % [r["code"], r["message"]])
		U.finish(self, fails)
		return
	var hash0: String = w.state_hash()
	print("tick0_state_hash=%s" % hash0)

	# get_time()의 달력 월·일 (윤년 없는 365일, empty.toml 시작 day 244 = 9월 1일): 시작, 12월 31일, 해 넘김
	var calendar := [[0, 244, 9, 1], [24 * 121, 365, 12, 31], [24, 1, 1, 1], [24 * 59 + 5, 60, 3, 1]]
	for c in calendar:
		w.advance(c[0])
		var t: Dictionary = w.get_time()
		if t["day_of_year"] != c[1] or t["month"] != c[2] or t["day"] != c[3]:
			fails.push_back("get_time at tick %d: %s (expected day %d = %d/%d)" % [w.get_tick(), t, c[1], c[2], c[3]])
	print("calendar_checks=%d last=%s" % [calendar.size(), w.get_time()])

	var core_version: int = ClassDB.class_call_static("SimWorld", "core_save_version")
	var peek: Dictionary = ClassDB.class_call_static("SimWorld", "peek_save_version", w.save_bytes())
	print("core_save_version=%d peek=%s" % [core_version, peek])
	if not peek["ok"] or peek["version"] != core_version or not peek["supported"]:
		fails.push_back("peek_save_version does not match core_save_version")

	if args["cli"] == "":
		print("cli_compare SKIPPED (no cli=)")
	else:
		var out := []
		var code := OS.execute(args["cli"], ["--params", ProjectSettings.globalize_path(args["params"]),
				"--scenario", ProjectSettings.globalize_path(SCENARIO), "--ticks", "0"], out, true)
		var cli_hash := ""
		for line in "".join(out).split("\n"):
			if line.begins_with("final_state_hash="):
				cli_hash = line.substr("final_state_hash=".length()).strip_edges()
		print("cli_tick0_state_hash=%s (exit %d)" % [cli_hash, code])
		if code != 0 or cli_hash != hash0:
			fails.push_back("tick 0 hash %s differs from CLI %s" % [hash0, cli_hash])
	U.finish(self, fails)
