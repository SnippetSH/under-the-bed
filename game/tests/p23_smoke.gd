# P2·P3 화면 로직 헤드리스 검사: SimRunner(자동 물 주기) + 명령 몇 개 + 빠른 저장·불러오기를 며칠 돌려요.
# 확인: (1) 자동 물 주기 기록이 하루 하나, 8시 틱(p3_auto_irrigation.gd와 같은 모양), (2) 거부 신호, (3) 저장→불러오기 후
# 이어 돌린 해시가 끊지 않은 실행과 같음, (4) 자동 처방 관전(일정표 켬, 자동 꺼짐)도 돌아감.
#   godot --headless --path game --script res://tests/p23_smoke.gd -- [days=30]
extends SceneTree

const SimRunner = preload("res://scripts/sim_runner.gd")
const SaveStore = preload("res://scripts/save_store.gd")
const Cfg = preload("res://scripts/cfg.gd")
const U = preload("res://tests/util.gd")

var rejected: Array = []


func _make(scenario: String, schedule: bool, auto_on: bool) -> Node:
	var r: Node = SimRunner.new()
	r._ready()  # 트리에 달지 않고 설정만 읽어요
	r.command_rejected.connect(func(m: String, _c: Dictionary): rejected.append(m))
	var res: Dictionary = r.start_new(scenario, schedule, auto_on, "test")
	if not res["ok"]:
		printerr("start failed %s" % res)
		quit(1)
	return r


func _init() -> void:
	var days := int(U.parse_args().get("days", "30"))
	var ticks := days * 24
	var fails := 0
	var scenario := "res://data/scenarios/m4/season_standard.toml"

	# A) 직접 플레이 + 자동 물 주기. 중간에 플레이어 명령 몇 개, 15일째에 저장.
	var a := _make(scenario, false, true)
	var half := 15 * 24
	a.step(half)
	a.submit({"action": "add_fertilizer", "area": [0, 0, 2, 20], "fertilizer": "calcium_nitrate", "g_per_m2": 4.0})
	a.submit({"action": "set_window", "layer": 0, "side": "left", "open_fraction": 0.5})
	a.submit({"action": "set_heater", "on": true, "setpoint_degC": 8.0})
	var bad: Dictionary = a.submit({"action": "plant", "seedling_grade": "B", "plants_per_m2": 8.0})  # 작물이 있어서 거부돼야 해요
	print("rejected_plant ok=%s msg=%s signal_count=%d" % [bad["ok"], bad.get("message", ""), rejected.size()])
	if bad["ok"] or rejected.size() != 1:
		fails += 1
	var sv := SaveStore.quick_save(a)
	print("quick_save %s tick=%d pending=%d hash=%s" % [sv["ok"], a.world.get_tick(), a.world.get_pending_command_count(), a.world.state_hash()])
	a.step(ticks - half)
	var final_a: String = a.world.state_hash()

	# 자동 물 주기 기록 모양: 하루 하나, 8시 틱, 플레이어 명령 (p3_auto_irrigation.gd와 같은 검사)
	var irrig := 0
	var shape_ok := true
	for e in a.world.get_command_log():
		if e["command"]["action"] == "irrigate" and e["source"] == "player":
			if int(e["tick"]) != 8 + 24 * irrig or e["command"].get("amount_mm", -1.0) != 4.0:
				shape_ok = false
			irrig += 1
	print("auto_irrigation days=%d irrigate_logged=%d one_per_day_at_hour_8=%s log_total=%d" % [days, irrig, shape_ok and irrig == days, a.world.get_command_log().size()])
	if not (shape_ok and irrig == days):
		fails += 1
	print("final_hash_A=%s" % final_a)

	# B) 저장에서 불러와 끝까지 이어 돌린 결과가 같아야 해요
	var b := _make(scenario, false, false)  # 일부러 다른 설정으로 만들고 불러오기가 덮어쓰는지 본다
	var ld := SaveStore.load_into(b)
	print("quick_load %s tick=%d auto=%s" % [ld["ok"], b.world.get_tick(), b.auto_irrigation])
	if not ld["ok"] or not b.auto_irrigation["enabled"]:
		fails += 1
	b.step(ticks - half)
	var final_b: String = b.world.state_hash()
	print("final_hash_B=%s %s" % [final_b, "MATCH" if final_a == final_b else "DIFF"])
	if final_a != final_b:
		fails += 1

	# C) 자동 처방 관전: 일정표 켬, 자동 꺼짐
	var c := _make(scenario, true, false)
	c.step(ticks)
	var sched := 0
	for e in c.world.get_command_log():
		if e["source"] == "schedule":
			sched += 1
	print("watch_mode ticks=%d schedule_commands=%d player_commands=%d hash=%s" % [ticks, sched, c.world.get_command_log().size() - sched, c.world.state_hash()])
	if sched == 0:
		fails += 1

	# D) 빈 밭 시나리오에서 정식 후 자라는지
	var d := _make("res://data/scenarios/empty.toml", false, true)
	var pr: Dictionary = d.submit({"action": "plant", "seedling_grade": "B", "plants_per_m2": 8.0})
	d.step(24 * 20)
	var vis: Dictionary = d.world.get_tile_visuals(0.8)
	print("empty_start plant_ok=%s present_cells=%d" % [pr["ok"], Array(vis["crop_present"]).count(1)])
	if not pr["ok"] or Array(vis["crop_present"]).count(1) != 80:
		fails += 1

	print("SMOKE %s" % ("FAIL" if fails > 0 else "PASS"))
	quit(1 if fails > 0 else 0)
