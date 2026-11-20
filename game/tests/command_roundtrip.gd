# E6 (m6-overview 10.1, 4.3): 명령 변환.
# (가) 17종 각각을 사전 → enqueue → 기록 → 사전으로 되돌려 기대한 정규형(area 채움, 숫자는 float)과 비교해요.
#      m6/all_commands(일정표 끔: 휴작기 끝, 점적 없음, 빈 밭)에서 시작해 정식 전후 순서로 넣어요.
# (나) 잘못된 사전(모르는 키·빠진 키·타입 불일치·모르는 action 등)과 코어가 거부하는 값은 전부 InvalidArgument이고
#      큐·기록이 늘지 않아요. can_enqueue도 같은 판정이에요. 거부된 명령은 이후 결과를 바꾸지 않아요(200틱 해시 비교).
#   godot --headless --path game --script res://tests/command_roundtrip.gd
extends SceneTree

const U = preload("res://tests/util.gd")
const SCENARIO := "res://data/scenarios/m6/all_commands.toml"

var bad := [
	{"action": "set_window", "layer": 9, "side": "left", "open_fraction": 0.5},  # 없는 겹 (코어)
	{"action": "set_window", "layer": 0, "side": "left", "open_fraction": 0.4},  # 허용 단계 아님 (코어)
	{"action": "add_fertilizer", "fertilizer": "gold_dust", "g_per_m2": 1.0},  # 모르는 id (코어)
	{"action": "irrigate", "amount_mm": -1.0},  # 음수 (코어)
	{"action": "irrigate", "area": [0, 15, 1, 10], "amount_mm": 1.0},  # 그리드 밖 (코어)
	{"action": "set_heater", "on": true, "setpoint_degC": 99.0},  # 범위 밖 (코어)
	{"action": "remove_plant", "area": [0, 0, 1, 1]},  # 작물 없음 (코어)
	{"action": "water", "amount_mm": 4.0},  # 모르는 action (로더)
	{"action": "irrigate", "amount_mm": 4.0, "foo": 1},  # 모르는 키 (로더)
	{"action": "irrigate"},  # 빠진 키 (로더)
	{"action": "set_window", "layer": 0.0, "side": "left", "open_fraction": 0.5},  # 정수 자리에 실수 (로더)
	{"action": "irrigate", "area": [0, 0, 4], "amount_mm": 1.0},  # area 길이 (로더)
	{"action": "set_fan", "on": 1},  # bool 자리에 정수 (로더)
	{"action": "set_window", "layer": 0, "side": "up", "open_fraction": 0.5},  # 모르는 side (로더)
	{"action": "irrigate", "amount_mm": 4.0, "tick": 3},  # tick은 래퍼가 정해요
	{"action": "irrigate", "amount_mm": 4.0, "repeat_count": 3},  # 반복은 일정표 전용
	{"action": "irrigate", "amount_mm": Vector2(1, 2)},  # 지원하지 않는 타입
	{"amount_mm": 4.0},  # action 없음 (로더)
	{},  # 빈 사전
	{1: "irrigate"},  # 글자가 아닌 키
]

# [넣는 사전, 기록에서 기대하는 정규형]. 순서대로 넣어요 (정식 뒤에 포기 명령)
var good := [
	[{"action": "set_cover_layers", "layers": 2}, {"action": "set_cover_layers", "layers": 2}],
	[{"action": "install_drip"}, {"action": "install_drip"}],
	[{"action": "irrigate", "amount_mm": 4}, {"action": "irrigate", "area": [0, 0, 4, 20], "amount_mm": 4.0}],
	[{"action": "add_compost", "area": [1, 2, 1, 3], "compost": "bark_compost", "kg_per_m2": 1.5},
	 {"action": "add_compost", "area": [1, 2, 1, 3], "compost": "bark_compost", "kg_per_m2": 1.5}],
	[{"action": "add_fertilizer", "fertilizer": "urea", "g_per_m2": 0.1},
	 {"action": "add_fertilizer", "area": [0, 0, 4, 20], "fertilizer": "urea", "g_per_m2": 0.1}],
	[{"action": "add_lime", "lime": "dolomite", "g_per_m2": 1.0 / 3.0},
	 {"action": "add_lime", "area": [0, 0, 4, 20], "lime": "dolomite", "g_per_m2": 1.0 / 3.0}],
	[{"action": "till"}, {"action": "till", "area": [0, 0, 4, 20]}],
	[{"action": "set_window", "layer": 1, "side": "right", "open_fraction": 0.7},
	 {"action": "set_window", "layer": 1, "side": "right", "open_fraction": 0.7}],
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
	[{"action": "remove_plant", "area": [3, 19, 1, 1]}, {"action": "remove_plant", "area": [3, 19, 1, 1]}],
]


func canon(d: Dictionary) -> String:
	return JSON.stringify(d, "", true, true)


func _init() -> void:
	var args := U.parse_args()
	var fails := PackedStringArray()

	# (가) 왕복
	var e: SimWorld = U.new_world(args["params"], SCENARIO, false)
	var replayed: SimWorld = U.new_world(args["params"], SCENARIO, false)
	var matched := 0
	var actions := {}
	for pair in good:
		var r: Dictionary = e.enqueue(pair[0])
		if not r["ok"]:
			fails.push_back("unexpected reject %s: %s" % [pair[0], r["message"]])
			continue
		var logged: Array = e.take_command_log()
		if logged.size() != 1 or logged[0]["source"] != "player" or canon(logged[0]["command"]) != canon(pair[1]):
			fails.push_back("round trip differs: %s -> %s" % [canon(pair[1]), canon(logged[0]["command"]) if logged.size() > 0 else "none"])
			continue
		# 기록의 사전을 그대로 다시 넣어도 같은 명령이 돼요
		var r2: Dictionary = replayed.enqueue(logged[0]["command"])
		var again: Array = replayed.take_command_log()
		if not r2["ok"] or again.size() != 1 or canon(again[0]["command"]) != canon(pair[1]):
			fails.push_back("re-enqueue of logged command differs: %s" % canon(pair[1]))
			continue
		matched += 1
		actions[pair[1]["action"]] = true
		if pair[0]["action"] == "plant":
			e.advance(1)  # 정식이 적용돼야 뒤의 포기 명령(적화·천적·포기 제거)이 받아들여져요
			replayed.advance(1)
	e.advance(1)
	replayed.advance(1)
	var applied := 0
	for ev in e.take_events():
		if ev["type"] == "command_applied":
			applied += 1
	print("round_trip matched=%d/%d actions=%d command_applied_events=%d" % [matched, good.size(), actions.size(), applied])
	if matched != good.size() or actions.size() != 17 or applied != good.size():
		fails.push_back("round trip matched %d/%d, actions %d/17, applied %d" % [matched, good.size(), actions.size(), applied])
	if e.state_hash() != replayed.state_hash():
		fails.push_back("re-enqueued commands give a different state")

	# (나) 거부: 코드, 큐·기록 불변, can_enqueue 같은 판정, 이후 결과 불변
	var a: SimWorld = U.new_world(args["params"], SCENARIO, false)
	var b: SimWorld = U.new_world(args["params"], SCENARIO, false)
	var ha := PackedStringArray([a.state_hash()])
	var hb := PackedStringArray([b.state_hash()])
	for t in range(200):
		if t == 20:
			var pending: int = a.get_pending_command_count()
			var log_size: int = a.get_command_log().size()
			for c in bad:
				var check: Dictionary = a.can_enqueue(c)
				var r: Dictionary = a.enqueue(c)
				print("reject ok=%s code=%s message=%s" % [r["ok"], r["code"], r["message"]])
				if r["ok"] or r["code"] != "InvalidArgument" or String(r["message"]) == "":
					fails.push_back("bad command %s: %s" % [c, r])
				if check["ok"] or check["code"] != r["code"] or check["message"] != r["message"]:
					fails.push_back("can_enqueue differs from enqueue for %s: %s vs %s" % [c, check, r])
			if a.get_pending_command_count() != pending or a.get_command_log().size() != log_size:
				fails.push_back("rejected commands changed the queue or log")
		a.advance(1)
		b.advance(1)
		ha.push_back(a.state_hash())
		hb.push_back(b.state_hash())
	var diff := U.compare(ha, hb)
	print("rejected=%d continue_ticks=200 %s" % [bad.size(), "MATCH" if diff == "" else diff])
	if diff != "":
		fails.push_back("rejected commands changed later ticks: %s" % diff)
	U.finish(self, fails)
