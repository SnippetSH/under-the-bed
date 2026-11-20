# P3 (d) 거부된 명령 + E6 축소판(명령 사전 왕복).
# (d) season_standard(일정표 켬)에서 틱 20에 잘못된 명령 여러 개를 넣어요: 전부 ok=false + code + message,
#     대기 명령 수·기록은 그대로, 시뮬레이션은 이어지고 모든 틱 해시가 "안 넣은 실행"과 같아야 해요.
# (E6) empty.toml에서 받아들여지는 15종을 넣고 기록의 사전이 기대한 정규형(area 채움)과 같은지 봐요.
#   godot --headless --path game --script res://tests/p3_commands.gd
extends SceneTree

const U = preload("res://tests/util.gd")

var bad := [
	{"action": "plant", "seedling_grade": "B", "plants_per_m2": 8.0},  # 작물이 있음 (코어)
	{"action": "set_window", "layer": 9, "side": "left", "open_fraction": 0.5},  # 없는 겹 (코어)
	{"action": "set_window", "layer": 0, "side": "left", "open_fraction": 0.4},  # 허용 단계 아님 (코어)
	{"action": "add_fertilizer", "fertilizer": "gold_dust", "g_per_m2": 1.0},  # 모르는 id (코어)
	{"action": "irrigate", "amount_mm": -1.0},  # 음수 (코어)
	{"action": "irrigate", "area": [0, 15, 1, 10], "amount_mm": 1.0},  # 그리드 밖 (코어)
	{"action": "water", "amount_mm": 4.0},  # 모르는 action (로더)
	{"action": "irrigate", "amount_mm": 4.0, "foo": 1},  # 모르는 키 (로더)
	{"action": "irrigate"},  # 빠진 키 (로더)
	{"action": "set_window", "layer": 0.0, "side": "left", "open_fraction": 0.5},  # 정수 자리에 실수 (로더)
	{"action": "irrigate", "area": [0, 0, 4], "amount_mm": 1.0},  # area 길이 (로더)
	{"action": "set_fan", "on": 1},  # bool 자리에 정수 (로더)
	{"action": "irrigate", "amount_mm": 4.0, "tick": 3},  # tick은 래퍼가 정함
	{"action": "irrigate", "amount_mm": Vector2(1, 2)},  # 지원하지 않는 타입
	{"amount_mm": 4.0},  # action 없음 (로더)
]

var good := [
	[{"action": "irrigate", "amount_mm": 4.0},
	 {"action": "irrigate", "area": [0, 0, 4, 20], "amount_mm": 4.0}],
	[{"action": "add_compost", "area": [1, 2, 1, 3], "compost": "bark_compost", "kg_per_m2": 1.5},
	 {"action": "add_compost", "area": [1, 2, 1, 3], "compost": "bark_compost", "kg_per_m2": 1.5}],
	[{"action": "add_fertilizer", "fertilizer": "urea", "g_per_m2": 0.1},
	 {"action": "add_fertilizer", "area": [0, 0, 4, 20], "fertilizer": "urea", "g_per_m2": 0.1}],
	[{"action": "add_lime", "lime": "dolomite", "g_per_m2": 1.0 / 3.0},
	 {"action": "add_lime", "area": [0, 0, 4, 20], "lime": "dolomite", "g_per_m2": 1.0 / 3.0}],
	[{"action": "till"}, {"action": "till", "area": [0, 0, 4, 20]}],
	[{"action": "set_window", "layer": 2, "side": "right", "open_fraction": 0.7},
	 {"action": "set_window", "layer": 2, "side": "right", "open_fraction": 0.7}],
	[{"action": "set_heater", "on": false, "setpoint_degC": 12.5},
	 {"action": "set_heater", "on": false, "setpoint_degC": 12.5}],
	[{"action": "set_mulch", "mulch": "white_film"},
	 {"action": "set_mulch", "area": [0, 0, 4, 20], "mulch": "white_film"}],
	[{"action": "apply_disinfectant", "disinfectant": "chloropicrin", "g_per_m2": 20},
	 {"action": "apply_disinfectant", "area": [0, 0, 4, 20], "disinfectant": "chloropicrin", "g_per_m2": 20.0}],
	[{"action": "add_inoculant", "inoculant": "bacillus_product", "g_per_m2": 5.0},
	 {"action": "add_inoculant", "area": [0, 0, 4, 20], "inoculant": "bacillus_product", "g_per_m2": 5.0}],
	[{"action": "plant", "seedling_grade": "A", "plants_per_m2": 7.5},
	 {"action": "plant", "seedling_grade": "A", "plants_per_m2": 7.5}],
	[{"action": "set_fan", "on": true}, {"action": "set_fan", "on": true}],
	[{"action": "thin_flowers", "area": [0, 0, 2, 2], "per_plant": 2.0},
	 {"action": "thin_flowers", "area": [0, 0, 2, 2], "per_plant": 2.0}],
	[{"action": "release_predators", "predator": "persimilis", "per_m2": 0.25},
	 {"action": "release_predators", "area": [0, 0, 4, 20], "predator": "persimilis", "per_m2": 0.25}],
	[{"action": "remove_plant", "area": [3, 19, 1, 1]},
	 {"action": "remove_plant", "area": [3, 19, 1, 1]}],
]


func canon(d: Dictionary) -> String:
	return JSON.stringify(d, "", true, true)


func _init() -> void:
	var ok := true
	# (d) 거부
	var a: Object = U.new_world("res://data/scenarios/m4/season_standard.toml", true)
	var b: Object = U.new_world("res://data/scenarios/m4/season_standard.toml", true)
	var ha := PackedStringArray([a.state_hash()])
	var hb := PackedStringArray([b.state_hash()])
	for t in range(200):
		if t == 20:
			var pending: int = a.get_pending_command_count()
			var log_size: int = a.get_command_log().size()
			for c in bad:
				var r: Dictionary = a.enqueue(c)
				var good_shape: bool = (not r["ok"]) and r["code"] != "" and String(r["message"]) != ""
				print("reject ok=%s code=%s message=%s" % [r["ok"], r["code"], r["message"]])
				if not good_shape:
					ok = false
			if a.get_pending_command_count() != pending or a.get_command_log().size() != log_size:
				printerr("rejected commands changed the queue or log")
				ok = false
		a.advance(1)
		b.advance(1)
		ha.push_back(a.state_hash())
		hb.push_back(b.state_hash())
	var diff := U.compare(ha, hb)
	print("rejected=%d continue_ticks=200 %s" % [bad.size(), "MATCH" if diff == "" else diff])
	ok = ok and diff == ""

	# (E6) 사전 왕복
	var e: Object = U.new_world("res://data/scenarios/empty.toml", false)
	var matched := 0
	for pair in good:
		var r: Dictionary = e.enqueue(pair[0])
		if not r["ok"]:
			printerr("unexpected reject %s: %s" % [pair[0], r["message"]])
			ok = false
			continue
		var logged: Array = e.take_command_log()
		if logged.size() != 1 or canon(logged[0]["command"]) != canon(pair[1]):
			printerr("round trip differs: %s -> %s" % [canon(pair[1]), canon(logged[0]["command"]) if logged.size() > 0 else "none"])
			ok = false
		else:
			matched += 1
		if pair[0]["action"] == "plant":
			e.advance(1)  # 정식이 적용돼야 뒤의 포기 명령이 의미 있어요
	e.advance(1)
	var events: Array = e.take_events()
	var applied := 0
	for ev in events:
		if ev["type"] == "command_applied":
			applied += 1
	print("round_trip matched=%d/%d command_applied_events=%d" % [matched, good.size(), applied])
	ok = ok and matched == good.size() and applied == good.size()
	quit(0 if ok else 1)
