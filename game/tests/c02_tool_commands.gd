# C2 (m6b-character-play 9.1): 가짜 입력으로 걸어가 도구 11종을 각각 한 번 써요 → 명령 기록에 1 × 1 영역 명령이 도구마다 하나.
# 덤: 행동하는 순간 "방금 한 일" 표시가 뜨고 다음 시간에 지워짐, 행동은 다음 시간 직전까지 코어에 안 들어감(명세 6장),
# 장치 앞 행동 키 → 조작 창(장치 8종, 출입문 포함), 장치 명령, 거부 알림이 한국어.
#   godot --headless --path game --script res://tests/c02_tool_commands.gd -- out=<폴더>
extends SceneTree

const U = preload("res://tests/util.gd")
const Tools = preload("res://scripts/char_tools.gd")
const Driver = preload("res://tests/char_driver.gd")
const SaveStore = preload("res://scripts/save_store.gd")

var fails := PackedStringArray()


func _ok(name: String, cond: bool, detail := "") -> void:
	if not cond:
		fails.push_back("%s %s" % [name, detail])
	print("%s %s %s" % ["ok  " if cond else "FAIL", name, detail])


func _init() -> void:
	var args := U.parse_args()
	SaveStore.dir = String(args["out"]).path_join("c02_saves")
	var notices: Array = []
	var s: Object = Driver.new_session("direct_play", false)   # 자동 물 주기 끔 (기록을 도구 명령만으로)
	if s == null:
		fails.push_back("start failed")
		U.finish(self, fails)
		return
	s.notice.connect(func(t: String): notices.append(t))
	var d: Object = Driver.new(s)
	var cols: int = s.cols

	# 도구마다 칸 하나. 순서: 흙 일(정식 전) → 정식(작업대) → 작물 일. [도구 index, 칸]
	var before_plant := [[2, 0], [3, 21], [4, 42], [6, 63], [5, 5], [7, 26]]   # 퇴비, 석회, 괭이, 소독제, 멀칭, 유익균
	var after_plant := [[1, 47], [0, 68], [10, 9], [8, 30], [9, 79]]           # 비료, 물뿌리개, 천적, 가위, 뽑기
	var used := {}
	var log0: int = s.world().get_command_log().size()
	for p in before_plant:
		var n0: int = s.held_count()
		d.use_tool(p[1], p[0])
		used[p[0]] = p[1]
		_ok("marker_after_%s" % Tools.TOOLS[p[0]]["id"], s.markers.has(p[1]) and s.held_count() == n0 + 1)
	_ok("held_not_in_core_before_next_hour", s.world().get_pending_command_count() == 0 and s.world().get_command_log().size() == log0)
	d.wait_ticks(1)
	_ok("markers_cleared_next_tick", s.markers.is_empty())
	# 정식: 작업대(모종 상자) 앞에서 행동 키 → 조작 창 → 하우스 전체 정식
	_ok("bench_plant_opens", d.open_device("bench_plant"))
	var pr: Dictionary = s.device_command({"action": "plant", "seedling_grade": "B", "plants_per_m2": 8.0})
	_ok("plant_accepted", pr["ok"], str(pr.get("message", "")))
	s.step_ticks(24 * 40)   # 꽃이 피고 과실이 맺힐 때까지 (가위·뽑기가 받아들여지게)
	for p in after_plant:
		d.use_tool(p[1], p[0])
		used[p[0]] = p[1]
	d.wait_ticks(1)   # 행동은 다음 틱 직전에 넣어요
	var log: Array = s.world().get_command_log().slice(log0)
	var tool_cmds := log.filter(func(e): return e["command"].has("area") and e["command"]["area"].size() == 4 and int(e["command"]["area"][2]) == 1 and int(e["command"]["area"][3]) == 1)
	for ti in range(Tools.TOOLS.size()):
		var t: Dictionary = Tools.TOOLS[ti]
		var cell: int = used.get(ti, -1)
		var hits := tool_cmds.filter(func(e): return e["command"]["action"] == t["action"])
		var good: bool = hits.size() == 1 and cell >= 0 and Array(hits[0]["command"]["area"]).map(func(v): return int(v)) == [cell / cols, cell % cols, 1, 1]
		_ok("C2_tool_%s_one_1x1_command" % t["id"], good, "hits=%d %s" % [hits.size(), hits[0]["command"] if hits.size() > 0 else ""])
	print("C2 log entries=%d (11 tool + 1 plant expected) rejected=%d raw=%s" % [log.size(), s.stats["rejected"], s.raw_rejections])
	_ok("C2_log_size_11_tools_plus_plant", log.size() == 12, "log=%d" % log.size())

	# 장치 8종 (출입문 포함): 그 앞에서 행동 키 → 조작 창
	for id in ["heater", "window_panel", "fan", "drip", "bench_cover", "auto_irrigation", "bench_plant", "door"]:
		_ok("device_%s_opens" % id, d.open_device(id))
	_ok("heater_cmd", s.device_command({"action": "set_heater", "on": true, "setpoint_degC": 8.0})["ok"])
	_ok("window_cmd", s.device_command({"action": "set_window", "layer": 0, "side": "right", "open_fraction": 0.5})["ok"])
	_ok("fan_cmd", s.device_command({"action": "set_fan", "on": true})["ok"])
	var rej0: int = s.stats["rejected"]
	var bad: Dictionary = s.device_command({"action": "plant", "seedling_grade": "B", "plants_per_m2": 8.0})
	var ascii := RegEx.create_from_string("[A-Za-z_]")
	var last_note: String = str(notices[-1]) if not notices.is_empty() else ""
	_ok("replant_rejected_at_once_with_korean_notice", not bad["ok"] and s.stats["rejected"] == rej0 + 1 and ascii.search(last_note) == null and str(s.raw_rejections[-1]).contains("live crop"), last_note)
	var log_dev0: int = s.world().get_command_log().size()
	d.wait_ticks(1)
	var dev_actions: Array = s.world().get_command_log().slice(log_dev0).map(func(e): return str(e["command"]["action"]))
	_ok("device_commands_logged_in_order", dev_actions == ["set_heater", "set_window", "set_fan"], str(dev_actions))
	print("frames=%d tick=%d" % [d.frames, s.world().get_tick()])
	s.dispose()
	U.finish(self, fails)
