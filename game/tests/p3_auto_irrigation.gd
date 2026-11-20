# P3 / E10 축소판: 일정표를 끈 season_standard에서 자동 물 주기만 켜요. SimRunner가 할 방식 그대로,
# 매 틱 직전에 지금 시각(get_time().hour)이 8시면 밭 전체 irrigate 4 mm를 플레이어 명령으로 넣어요.
# 30일 뒤 기록을 [[schedule]]로 내보내 sim-cli로 재생 → 모든 틱 해시가 같고, 기록은 하루 하나씩이에요.
#   godot --headless --path game --script res://tests/p3_auto_irrigation.gd -- out=<폴더> cli=<sim-cli.exe> [days=30]
extends SceneTree

const U = preload("res://tests/util.gd")
const SCENARIO := "res://data/scenarios/m4/season_standard.toml"
const AUTO_HOUR := 8  # 게임 쪽 설정 기본값 (m6-overview 6.5, 게임용 임의값)
const AUTO_MM := 4.0


func _init() -> void:
	var args := U.parse_args()
	var days := int(args.get("days", "30"))
	var ticks := days * 24
	var w: Object = U.new_world(SCENARIO, false)
	if w == null:
		quit(1)
		return
	var hashes := PackedStringArray([w.state_hash()])
	for t in range(ticks):
		if int(w.get_time()["hour"]) == AUTO_HOUR:
			var r: Dictionary = w.enqueue({"action": "irrigate", "amount_mm": AUTO_MM})
			if not r["ok"]:
				printerr("auto irrigation rejected: %s" % r)
				quit(1)
				return
		w.advance(1)
		hashes.push_back(w.state_hash())
	var log: Array = w.get_command_log()
	var ok := log.size() == days
	for i in range(log.size()):
		var e: Dictionary = log[i]
		if int(e["tick"]) != AUTO_HOUR + 24 * i or e["command"]["action"] != "irrigate" or e["source"] != "player":
			ok = false
	print("days=%d ticks=%d log=%d one_irrigate_per_day=%s final=%s" % [days, ticks, log.size(), ok, hashes[-1]])
	var replay := U.write_replay(args["out"], "p3_auto_irrigation.toml", SCENARIO, log)
	if replay == "" or not ok:
		quit(1)
		return
	if args["cli"] == "":
		print("engine_to_cli SKIPPED (no cli=)")
		quit(0)
		return
	var cli_hashes := U.run_cli(args["cli"], replay, ticks, ProjectSettings.globalize_path(args["out"].path_join("p3_auto_irrigation_cli.csv")))
	var diff := U.compare(hashes, cli_hashes)
	print("engine_to_cli rows=%d %s" % [cli_hashes.size(), "MATCH" if diff == "" else diff])
	quit(0 if diff == "" else 1)
