# C4 (m6b-character-play 9.1, 8장): 가짜 캐릭터 판(char_plan.gd: 도구, 누른 채 걷기, 장치, 자러 가기, 며칠 맡기기,
# 맡기기 중 끊기, 속도 단추, 1시간 건너뛰기, 자동 물 주기)을 돌리고 그 판의 명령 기록을
#  (1) 새 엔진 월드에 그 틱 직전에 seq 순서로 다시 넣어 재생, (2) [[schedule]]로 내보내 sim-cli로 재생 (M6a E4와 같은 도구)
# → 모든 틱의 해시가 같아야 해요. 덤: 자러 가기·맡기기가 기상 시각에 멈춤(G5 자동판), 1시간 건너뛰기, 기록의 1 × 1 도구 명령.
#   godot --headless --path game --script res://tests/c04_play_replays_on_cli.gd -- out=<폴더> cli=<sim-cli>
extends SceneTree

const U = preload("res://tests/util.gd")
const Plan = preload("res://tests/char_plan.gd")
const Driver = preload("res://tests/char_driver.gd")
const Cfg = preload("res://scripts/cfg.gd")
const SaveStore = preload("res://scripts/save_store.gd")

var fails := PackedStringArray()
var hashes := PackedStringArray()


func _ok(name: String, cond: bool, detail := "") -> void:
	if not cond:
		fails.push_back("%s %s" % [name, detail])
	print("%s %s %s" % ["ok  " if cond else "FAIL", name, detail])


func _on_tick(s: Object) -> void:
	hashes.push_back(s.world().state_hash())


func _init() -> void:
	var args := U.parse_args()
	SaveStore.dir = String(args["out"]).path_join("c04_saves")
	var a: Object = Driver.new_session(Plan.SCENARIO_ID)
	if a == null:
		fails.push_back("start failed")
		U.finish(self, fails)
		return
	hashes.push_back(a.world().state_hash())
	a.tick_hook = _on_tick.bind(a)
	var t_start := Time.get_ticks_msec()
	var ra: Dictionary = Plan.run(a, 0)
	a.tick_hook = Callable()
	if not ra["ok"]:
		fails.push_back("plan failed at op %d" % ra["at"])
		a.dispose()
		U.finish(self, fails)
		return
	var ticks: int = a.world().get_tick()
	var log: Array = a.world().get_command_log()
	print("run ticks=%d log=%d final=%s frames=%d wall_ms=%d" % [ticks, log.size(), hashes[-1], ra["frames"], Time.get_ticks_msec() - t_start])
	_ok("hash_per_tick_collected", hashes.size() == ticks + 1, "rows=%d ticks=%d" % [hashes.size(), ticks])
	# 자러 가기·맡기기가 기상 시각에 멈췄는지 (stops: [틱, 시각, 돌린 틱])
	var wake: int = a.wake_hour
	var stop_ok := true
	for st in ra["stops"]:
		if int(st[1]) != wake:
			stop_ok = false
	print("fast_forward stops [tick, hour, ticks run] = %s" % [ra["stops"]])
	_ok("sleep_and_skip_stop_at_wake_hour", stop_ok and ra["stops"].size() == 5)
	_ok("skip_3_days_is_3_wakeups", ra["stops"].size() >= 2 and int(ra["stops"][1][2]) > 48 and int(ra["stops"][1][2]) <= 72)
	# 열흘(240틱) 맡기기를 160틱 돈 뒤에 끊으면 그다음 기상 시각(168틱)에 멈춰요 (char_plan, 끊는 때는 틱 기준)
	_ok("skip_10_interrupted_stops_at_next_wake", ra["stops"].size() >= 3 and int(ra["stops"][2][2]) == 168, "ran=%d" % (int(ra["stops"][2][2]) if ra["stops"].size() >= 3 else -1))
	_ok("skip_hour_runs_one_tick_applies_pending_refused_while_paused", ra["skip_checks"].size() == 3 and not ra["skip_checks"].has(false), str(ra["skip_checks"]))
	var tool_1x1 := 0
	var auto_irrigate := 0
	for e in log:
		var c: Dictionary = e["command"]
		if c.has("area") and int(c["area"][2]) == 1 and int(c["area"][3]) == 1:
			tool_1x1 += 1
		elif c["action"] == "irrigate":
			auto_irrigate += 1
	print("log: tool_commands_1x1=%d auto_irrigation=%d total=%d" % [tool_1x1, auto_irrigate, log.size()])
	_ok("log_has_tool_and_auto_irrigation_commands", tool_1x1 > 60 and auto_irrigate > 10)

	# ---- (1) 엔진 → 엔진 ----
	var scenario: String = Cfg.scenario(Plan.SCENARIO_ID)["path"]
	var w2: SimWorld = U.new_world(args["params"], scenario, false)
	var h2 := PackedStringArray([w2.state_hash()])
	var li := 0
	for t in range(ticks):
		while li < log.size() and int(log[li]["tick"]) == t:
			var r: Dictionary = w2.enqueue(log[li]["command"])
			if not r["ok"]:
				fails.push_back("engine replay enqueue failed tick %d %s" % [t, r])
			li += 1
		w2.advance(1)
		h2.push_back(w2.state_hash())
	var diff := U.compare(hashes, h2)
	_ok("C4_engine_to_engine_all_ticks", diff == "", "rows=%d %s" % [h2.size(), "MATCH" if diff == "" else diff])

	# ---- (2) 엔진 → sim-cli ----
	var replay := U.write_replay(args["out"], "c04_character_replay.toml", scenario, log)
	U.write_text(ProjectSettings.globalize_path(String(args["out"]).path_join("c04_character_engine.csv")), U.hashes_to_csv(hashes))
	if replay == "":
		fails.push_back("cannot write the replay scenario")
	elif args["cli"] == "":
		print("C4_engine_to_cli SKIPPED (no cli=) replay=%s" % replay)
	else:
		var ch := U.run_cli(args["cli"], args["params"], replay, ticks,
				ProjectSettings.globalize_path(String(args["out"]).path_join("c04_character_cli.csv")))
		var d2 := U.compare(hashes, ch)
		_ok("C4_engine_to_cli_all_ticks", d2 == "" and ch.size() == hashes.size(), "rows=%d %s replay=%s" % [ch.size(), "MATCH" if d2 == "" else d2, replay])
	a.dispose()
	U.finish(self, fails)
