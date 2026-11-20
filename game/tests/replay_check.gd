# E4 (m6-overview 10.1): 명령 기록 재생 (엔진 → 엔진, 엔진 → CLI).
# 일정표를 끈 m6/all_commands(빈 밭, 휴작기 끝, 점적 없음)에 고정된 "가짜 플레이어"가 17종 명령을 전부 넣고
# (거부되는 것 포함, 넣기 전에 can_enqueue 판정이 enqueue와 같은지도 봐요) 720틱을 돌려요.
# 명령 기록을 (1) 새 엔진 월드에 그 틱 직전에 seq 순서로 다시 넣어 재생, (2) [[schedule]]로 내보내 sim-cli로 재생
# → 세 실행의 모든 틱 해시가 같아야 해요.
#   godot --headless --path game --script res://tests/replay_check.gd -- out=<폴더> cli=<sim-cli> [ticks=720]
extends SceneTree

const U = preload("res://tests/util.gd")
const SCENARIO := "res://data/scenarios/m6/all_commands.toml"
const ACTIONS := ["irrigate", "add_compost", "add_fertilizer", "add_lime", "till", "set_window", "set_heater",
		"set_cover_layers", "thin_flowers", "set_mulch", "apply_disinfectant", "add_inoculant", "plant", "set_fan",
		"install_drip", "remove_plant", "release_predators"]

# 틱 -> 그 틱 직전에 넣을 [명령, 받아들여질지]
var plan := {
	0: [
		[{"action": "set_cover_layers", "layers": 2}, true],  # day 240: 휴작기 안
		[{"action": "add_compost", "compost": "mature_manure", "kg_per_m2": 2.0}, true],
		[{"action": "add_lime", "area": [0, 0, 2, 10], "lime": "calcium_carbonate", "g_per_m2": 50.0}, true],
		[{"action": "till", "area": [0, 0, 4, 20]}, true],
	],
	3: [[{"action": "irrigate", "amount_mm": 20.0}, true]],
	4: [
		[{"action": "apply_disinfectant", "area": [2, 0, 2, 20], "disinfectant": "dazomet", "g_per_m2": 30.0}, true],
		[{"action": "set_mulch", "mulch": "clear_film"}, true],
	],
	5: [[{"action": "add_inoculant", "inoculant": "trichoderma_product", "g_per_m2": 10.0}, true]],
	6: [[{"action": "install_drip"}, true]],
	7: [[{"action": "install_drip"}, false]],  # 이미 깔려 있어요
	96: [
		[{"action": "set_cover_layers", "layers": 3}, false],  # day 244: 휴작기 밖
		[{"action": "set_mulch", "area": [0, 0, 1, 20], "mulch": "none"}, true],
	],
	120: [[{"action": "plant", "seedling_grade": "B", "plants_per_m2": 8.0}, true]],
	121: [
		[{"action": "plant", "seedling_grade": "B", "plants_per_m2": 8.0}, false],  # 작물이 있어요
		[{"action": "set_fan", "on": true}, true],
	],
	130: [[{"action": "set_heater", "on": true, "setpoint_degC": 8.0}, true]],
	140: [[{"action": "add_fertilizer", "area": [0, 0, 4, 10], "fertilizer": "calcium_nitrate", "g_per_m2": 5.0}, true]],
	150: [
		[{"action": "set_window", "layer": 0, "side": "left", "open_fraction": 0.5}, true],
		[{"action": "set_window", "layer": 2, "side": "left", "open_fraction": 0.5}, false],  # 2겹이라 겹 2가 없어요
	],
	158: [[{"action": "set_window", "layer": 0, "side": "left", "open_fraction": 0.0}, true]],
	200: [[{"action": "release_predators", "area": [1, 1, 1, 1], "predator": "persimilis", "per_m2": 2.0}, true]],
	300: [
		[{"action": "irrigate", "area": [3, 18, 2, 2], "amount_mm": 3.0}, false],  # 그리드 밖 (코어)
		[{"action": "water", "amount_mm": 3.0}, false],  # 모르는 action (래퍼·로더)
	],
	400: [[{"action": "thin_flowers", "per_plant": 1.0}, true]],
	500: [[{"action": "remove_plant", "area": [3, 19, 1, 1]}, true]],
	600: [[{"action": "remove_plant", "area": [3, 19, 1, 1]}, false]],  # 이미 뽑았어요
}


func _init() -> void:
	var args := U.parse_args()
	var ticks := int(args.get("ticks", "720"))
	var fails := PackedStringArray()
	var w: SimWorld = U.new_world(args["params"], SCENARIO, false)
	if w == null:
		fails.push_back("load failed")
		U.finish(self, fails)
		return
	var hashes := PackedStringArray([w.state_hash()])
	var accepted := 0
	var rejected := 0
	var seen := {}
	for t in range(ticks):
		var cmds: Array = plan.get(t, [])
		if t >= 128 and t % 24 == 8:
			cmds = cmds + [[{"action": "irrigate", "amount_mm": 4.0}, true]]
		for c in cmds:
			var check: Dictionary = w.can_enqueue(c[0])
			var r: Dictionary = w.enqueue(c[0])
			if r["ok"] != c[1] or check["ok"] != r["ok"] or check["code"] != r["code"]:
				fails.push_back("tick %d %s: expected ok=%s, can_enqueue=%s, enqueue=%s" % [t, c[0], c[1], check, r])
			if r["ok"]:
				accepted += 1
				seen[c[0]["action"]] = true
			else:
				rejected += 1
				print("rejected tick=%d action=%s code=%s message=%s" % [t, c[0]["action"], r["code"], r["message"]])
		var a: Dictionary = w.advance(1)
		if not a["ok"]:
			fails.push_back("advance failed: %s" % a)
			U.finish(self, fails)
			return
		hashes.push_back(w.state_hash())
	for action in ACTIONS:
		if not seen.has(action):
			fails.push_back("action %s was never accepted" % action)
	var log: Array = w.get_command_log()
	print("ticks=%d accepted=%d rejected=%d actions=%d log=%d final=%s" % [ticks, accepted, rejected, seen.size(), log.size(), hashes[-1]])
	if log.size() != accepted:
		fails.push_back("log size %d != accepted %d" % [log.size(), accepted])

	# (1) 엔진 -> 엔진: 새 월드에 기록을 그 틱 직전에 seq 순서로 넣어요
	var w2: SimWorld = U.new_world(args["params"], SCENARIO, false)
	var hashes2 := PackedStringArray([w2.state_hash()])
	var li := 0
	for t in range(ticks):
		while li < log.size() and int(log[li]["tick"]) == t:
			var r: Dictionary = w2.enqueue(log[li]["command"])
			if not r["ok"] or int(r["seq"]) != int(log[li]["seq"]):
				fails.push_back("engine replay enqueue failed at tick %d: %s" % [t, r])
			li += 1
		w2.advance(1)
		hashes2.push_back(w2.state_hash())
	var diff := U.compare(hashes, hashes2)
	print("engine_to_engine rows=%d %s" % [hashes2.size(), "MATCH" if diff == "" else diff])
	if diff != "":
		fails.push_back("engine to engine: %s" % diff)

	# (2) 엔진 -> CLI
	var replay := U.write_replay(args["out"], "replay_check.toml", SCENARIO, log)
	if replay == "":
		fails.push_back("cannot write replay scenario")
	elif args["cli"] == "":
		print("engine_to_cli SKIPPED (no cli=) replay=%s" % replay)
	else:
		var cli_hashes := U.run_cli(args["cli"], args["params"], replay, ticks,
				ProjectSettings.globalize_path(args["out"].path_join("replay_check_cli.csv")))
		diff = U.compare(hashes, cli_hashes)
		print("engine_to_cli rows=%d %s replay=%s" % [cli_hashes.size(), "MATCH" if diff == "" else diff, replay])
		if diff != "":
			fails.push_back("engine to CLI: %s" % diff)
	U.finish(self, fails)
