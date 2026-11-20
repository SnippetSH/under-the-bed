# P3 / E4 축소판: 일정표를 끈 빈 밭(empty.toml)에 "가짜 플레이어"가 17종 명령을 넣으며(거부되는 것 포함) N틱을 돌리고,
# 명령 기록을 (1) 엔진에서 다시 넣어 재생, (2) [[schedule]]로 내보내 sim-cli로 재생 → 모든 틱 해시가 같아야 해요.
#   godot --headless --path game --script res://tests/p3_replay.gd -- out=<폴더> cli=<sim-cli.exe> [ticks=720]
extends SceneTree

const U = preload("res://tests/util.gd")
const SCENARIO := "res://data/scenarios/empty.toml"

# 틱 -> 그 틱 직전에 넣을 명령들. 거부 예상은 expect_ok = false
var plan := {
	0: [
		[{"action": "add_compost", "compost": "mature_manure", "kg_per_m2": 2.0}, true],
		[{"action": "add_lime", "area": [0, 0, 2, 10], "lime": "calcium_carbonate", "g_per_m2": 50.0}, true],
		[{"action": "till", "area": [0, 0, 4, 20]}, true],
	],
	5: [
		[{"action": "apply_disinfectant", "area": [2, 0, 2, 20], "disinfectant": "dazomet", "g_per_m2": 30.0}, true],
		[{"action": "set_mulch", "mulch": "black_film"}, true],
	],
	10: [
		[{"action": "plant", "seedling_grade": "B", "plants_per_m2": 8.0}, true],
		[{"action": "set_fan", "on": true}, true],
		[{"action": "set_heater", "on": true, "setpoint_degC": 8.0}, true],
	],
	11: [[{"action": "plant", "seedling_grade": "B", "plants_per_m2": 8.0}, false]],  # 작물이 있어요
	100: [
		[{"action": "add_fertilizer", "area": [0, 0, 4, 10], "fertilizer": "calcium_nitrate", "g_per_m2": 5.0}, true],
		[{"action": "set_window", "layer": 0, "side": "left", "open_fraction": 0.5}, true],
		[{"action": "set_window", "layer": 1, "side": "right", "open_fraction": 0.3}, true],
	],
	200: [
		[{"action": "add_inoculant", "inoculant": "trichoderma_product", "g_per_m2": 10.0}, true],
		[{"action": "release_predators", "area": [1, 1, 1, 1], "predator": "persimilis", "per_m2": 2.0}, true],
	],
	300: [
		[{"action": "thin_flowers", "per_plant": 1.0}, true],
		[{"action": "remove_plant", "area": [3, 19, 1, 1]}, true],
		[{"action": "set_mulch", "area": [0, 0, 1, 1], "mulch": "none"}, true],
		[{"action": "set_window", "layer": 0, "side": "left", "open_fraction": 0.0}, true],
	],
	310: [
		[{"action": "install_drip"}, false],  # 이미 깔려 있어요
		[{"action": "set_cover_layers", "layers": 2}, false],  # 휴작기 밖 (9월)
		[{"action": "irrigate", "area": [3, 18, 2, 2], "amount_mm": 3.0}, false],  # 그리드 밖
	],
}


func _init() -> void:
	var args := U.parse_args()
	var ticks := int(args.get("ticks", "720"))
	var w: Object = U.new_world(SCENARIO, false)
	if w == null:
		quit(1)
		return
	var hashes := PackedStringArray([w.state_hash()])
	var accepted := 0
	var rejected := 0
	for t in range(ticks):
		var cmds: Array = plan.get(t, [])
		if t % 24 == 8:
			cmds = cmds + [[{"action": "irrigate", "amount_mm": 4.0}, true]]
		for c in cmds:
			var r: Dictionary = w.enqueue(c[0])
			if r["ok"] != c[1]:
				printerr("tick %d %s: expected ok=%s got %s" % [t, c[0], c[1], r])
				quit(1)
				return
			if r["ok"]:
				accepted += 1
			else:
				rejected += 1
				print("rejected tick=%d action=%s code=%s message=%s" % [t, c[0]["action"], r["code"], r["message"]])
		var a: Dictionary = w.advance(1)
		if not a["ok"]:
			printerr("advance failed: %s" % a)
			quit(1)
			return
		hashes.push_back(w.state_hash())
	var log: Array = w.get_command_log()
	print("ticks=%d accepted=%d rejected=%d log=%d final=%s" % [ticks, accepted, rejected, log.size(), hashes[-1]])
	if log.size() != accepted:
		printerr("log size %d != accepted %d" % [log.size(), accepted])
		quit(1)
		return

	# (1) 엔진 -> 엔진: 새 월드에 기록을 그 틱 직전에 seq 순서로 넣어요
	var w2: Object = U.new_world(SCENARIO, false)
	var hashes2 := PackedStringArray([w2.state_hash()])
	var li := 0
	for t in range(ticks):
		while li < log.size() and int(log[li]["tick"]) == t:
			var r: Dictionary = w2.enqueue(log[li]["command"])
			if not r["ok"] or int(r["seq"]) != int(log[li]["seq"]):
				printerr("engine replay enqueue failed at tick %d: %s" % [t, r])
				quit(1)
				return
			li += 1
		w2.advance(1)
		hashes2.push_back(w2.state_hash())
	var diff := U.compare(hashes, hashes2)
	print("engine_to_engine rows=%d %s" % [hashes2.size(), "MATCH" if diff == "" else diff])
	if diff != "":
		quit(1)
		return

	# (2) 엔진 -> CLI
	var replay := U.write_replay(args["out"], "p3_replay.toml", SCENARIO, log)
	if replay == "":
		quit(1)
		return
	U.write_text(args["out"].path_join("p3_replay_engine.csv"), U.hashes_to_csv(hashes))
	if args["cli"] == "":
		print("engine_to_cli SKIPPED (no cli=) replay=%s" % replay)
		quit(0)
		return
	var cli_hashes := U.run_cli(args["cli"], replay, ticks, ProjectSettings.globalize_path(args["out"].path_join("p3_replay_cli.csv")))
	diff = U.compare(hashes, cli_hashes)
	print("engine_to_cli rows=%d %s replay=%s" % [cli_hashes.size(), "MATCH" if diff == "" else diff, replay])
	quit(0 if diff == "" else 1)
