# E10 (m6-overview 10.1, 6.5 결정 D5): 자동 물 주기.
# 일정표가 없는 직접 플레이 판(m6/direct_play)에서 자동 물 주기만 켜요. SimRunner가 하는 방식 그대로
# 매 틱 직전에 지금 시각(get_time().hour)이 8시면 밭 전체 irrigate 4 mm를 플레이어 명령으로 넣어요.
# 30일 뒤 기록을 [[schedule]]로 내보내 sim-cli로 다시 돌리면 모든 틱 해시가 같고, 기록은 하루에 irrigate 하나씩이에요.
# (SimRunner 자체의 설정 변경·세이브 동작은 게임 스크립트 쪽 검사 몫이에요.)
#   godot --headless --path game --script res://tests/auto_irrigation.gd -- out=<폴더> cli=<sim-cli> [days=30]
extends SceneTree

const U = preload("res://tests/util.gd")
const SCENARIO := "res://data/scenarios/m6/direct_play.toml"
const AUTO_HOUR := 8  # 게임 쪽 설정 기본값 (m6-overview 6.5, 게임용 임의값)
const AUTO_MM := 4.0


func _init() -> void:
	var args := U.parse_args()
	var days := int(args.get("days", "30"))
	var ticks := days * 24
	var fails := PackedStringArray()
	var w: SimWorld = U.new_world(args["params"], SCENARIO, false)
	if w == null:
		fails.push_back("load failed")
		U.finish(self, fails)
		return
	var hashes := PackedStringArray([w.state_hash()])
	for t in range(ticks):
		var time: Dictionary = w.get_time()
		if int(time["hour"]) == AUTO_HOUR:
			var r: Dictionary = w.enqueue({"action": "irrigate", "amount_mm": AUTO_MM})
			if not r["ok"]:
				fails.push_back("auto irrigation rejected: %s" % r)
				break
		w.advance(1)
		hashes.push_back(w.state_hash())
	var log: Array = w.get_command_log()
	var start_hour := int(U.new_world(args["params"], SCENARIO, false).get_time()["hour"])
	var first_tick := (AUTO_HOUR - start_hour + 24) % 24
	var shape_ok := log.size() == days
	for i in range(log.size()):
		var e: Dictionary = log[i]
		if int(e["tick"]) != first_tick + 24 * i or e["command"]["action"] != "irrigate" or e["source"] != "player" \
				or float(e["command"]["amount_mm"]) != AUTO_MM:
			shape_ok = false
	print("days=%d ticks=%d log=%d one_irrigate_per_day=%s final=%s" % [days, ticks, log.size(), shape_ok, hashes[-1]])
	if not shape_ok:
		fails.push_back("log is not one irrigate per day at hour %d" % AUTO_HOUR)
	var replay := U.write_replay(args["out"], "auto_irrigation.toml", SCENARIO, log)
	if replay == "":
		fails.push_back("cannot write replay scenario")
	elif args["cli"] == "":
		print("engine_to_cli SKIPPED (no cli=)")
	else:
		var cli_hashes := U.run_cli(args["cli"], args["params"], replay, ticks,
				ProjectSettings.globalize_path(args["out"].path_join("auto_irrigation_cli.csv")))
		var diff := U.compare(hashes, cli_hashes)
		print("engine_to_cli rows=%d %s" % [cli_hashes.size(), "MATCH" if diff == "" else diff])
		if diff != "":
			fails.push_back("engine to CLI: %s" % diff)
	U.finish(self, fails)
