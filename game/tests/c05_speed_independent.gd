# C5 (m6b-character-play 9.1, 7.1·8장): C4의 가짜 캐릭터 판을 일하는 속도 3가지(설정값, 10배, 100배)로 돌려요.
# 가짜 입력은 틱 기준이에요(Driver: 걷기는 시간을 흘리지 않고, 기다리기·자러 가기·맡기기만 시계를 흘려요) → 최종 해시·틱 수·기록이 같음.
# 덤: 속도 단추 ×1~×4가 1시간 = 60·30·20·15초(R16, 설정 파일 값), 달리기 배수 1.75(R17).
#   godot --headless --path game --script res://tests/c05_speed_independent.gd -- out=<폴더>
extends SceneTree

const U = preload("res://tests/util.gd")
const Plan = preload("res://tests/char_plan.gd")
const Driver = preload("res://tests/char_driver.gd")
const Cfg = preload("res://scripts/cfg.gd")
const SaveStore = preload("res://scripts/save_store.gd")

var fails := PackedStringArray()


func _ok(name: String, cond: bool, detail := "") -> void:
	if not cond:
		fails.push_back("%s %s" % [name, detail])
	print("%s %s %s" % ["ok  " if cond else "FAIL", name, detail])


func _init() -> void:
	var args := U.parse_args()
	SaveStore.dir = String(args["out"]).path_join("c05_saves")
	var base := float(Cfg.get_v("char_time", "seconds_per_game_hour"))
	print("config seconds_per_game_hour=%.0f speed_multipliers=%s" % [base, Cfg.get_v("char_time", "speed_multipliers")])
	var results: Array = []
	for mul in [1.0, 10.0, 100.0]:
		var s: Object = Driver.new_session(Plan.SCENARIO_ID)
		if s == null:
			fails.push_back("start failed")
			break
		if mul == 1.0:
			var secs := []
			for i in range(s.speed_multipliers.size()):
				s.set_speed(i)
				secs.append(int(round(s.hour_seconds())))
			s.set_speed(0)
			_ok("speed_buttons_60_30_20_15_seconds", secs == [60, 30, 20, 15], str(secs))
			_ok("run_factor_1_75", is_equal_approx(s.run_factor, 1.75))
		s.base_seconds_per_hour = base / mul
		var t0 := Time.get_ticks_msec()
		var r: Dictionary = Plan.run(s, 0)
		var res := {"ok": r["ok"], "hash": s.world().state_hash(), "tick": s.world().get_tick(),
				"log": JSON.stringify(s.world().get_command_log()), "ms": Time.get_ticks_msec() - t0}
		print("seconds_per_hour=%.2f ok=%s ticks=%d final=%s wall_ms=%d" % [base / mul, res["ok"], res["tick"], res["hash"], res["ms"]])
		results.append(res)
		s.dispose()
	if results.size() == 3:
		for i in [1, 2]:
			var same: bool = results[i]["ok"] and results[i]["hash"] == results[0]["hash"] and results[i]["tick"] == results[0]["tick"] and results[i]["log"] == results[0]["log"]
			_ok("C5_speed_x%d_same_final_hash_ticks_and_log" % [10 if i == 1 else 100], same, "%s vs %s" % [results[i]["hash"], results[0]["hash"]])
	U.finish(self, fails)
