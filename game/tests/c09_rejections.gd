# C9 (m6b-character-play 9.1, 2.3·5.2): 거부될 행동을 종류별로 해요 → 누른 즉시(시간이 흐르기 전에, 바인딩 can_enqueue)
# 한국어 문장 한 줄이 알림으로 뜨고, 영문자·밑줄(식별자·영어 원문)이 플레이어 화면 알림에 없고, 원문은 개발자 기록에만 가요.
# 날짜·범위는 카탈로그 값에서 만들어요(하드코딩 없음). 캐릭터 화면을 띄워 알림 줄에 영어가 없는지도 봐요.
#   godot --headless --path game --script res://tests/c09_rejections.gd -- out=<폴더>
extends SceneTree

const U = preload("res://tests/util.gd")
const Driver = preload("res://tests/char_driver.gd")
const Tools = preload("res://scripts/char_tools.gd")
const RejectText = preload("res://scripts/reject_text.gd")
const SaveStore = preload("res://scripts/save_store.gd")
const Session = preload("res://scripts/session.gd")

var fails := PackedStringArray()
var ascii := RegEx.create_from_string("[A-Za-z_]")


func _ok(name: String, cond: bool, detail := "") -> void:
	if not cond:
		fails.push_back("%s %s" % [name, detail])
	print("%s %s %s" % ["ok  " if cond else "FAIL", name, detail])


# 한 가지를 일으키고 알림 한 줄을 받아요. f: 거부를 일으키는 Callable
func _case(s: Object, notes: Array, name: String, f: Callable, must_contain := "") -> void:
	var n0 := notes.size()
	var r0: int = s.raw_rejections.size()
	var t0: int = s.world().get_tick()
	f.call()
	var got: Array = notes.slice(n0)
	var shown: String = got[-1] if not got.is_empty() else ""
	var raw: String = s.raw_rejections[-1] if s.raw_rejections.size() > r0 else ""
	var ok := got.size() == 1 and ascii.search(shown) == null and shown != "" and raw != "" and (must_contain == "" or shown.contains(must_contain))
	if name != "auto_irrigation":
		ok = ok and s.world().get_tick() == t0 and s.held_count() == 0   # 누른 즉시, 붙잡지 않음
	_ok("reject_%s" % name, ok, "shown=\"%s\" | dev_raw=\"%s\"" % [shown, raw])


func _tool(s: Object, tool_id: String, cell: int, amount: float) -> void:
	var ti := -1
	for i in range(Tools.TOOLS.size()):
		if Tools.TOOLS[i]["id"] == tool_id:
			ti = i
	s.select_tool(ti)
	var old: Dictionary = s.tool_settings[ti].duplicate()
	if s.tool_settings[ti].has("amount"):
		s.tool_settings[ti]["amount"] = amount
	s.use_tool_on(cell)
	s.tool_settings[ti] = old


func _initialize() -> void:
	var args := U.parse_args()
	SaveStore.dir = String(args["out"]).path_join("c09_saves")
	# 심어 둔 밭: 작물 있음, 9월(겹수 못 바꾸는 때), 점적 깔림, 비닐 3겹
	var s: Object = Driver.new_session("char_planted", false)
	var notes: Array = []
	s.notice.connect(func(t: String): notes.append(t))
	var cat: Dictionary = s.catalog
	var w: Array = Array(cat["cover_change_window_doy"])
	var date_from := RejectText.doy_to_text(int(w[0]))
	var date_to := RejectText.doy_to_text(int(w[1]))
	print("cover window from catalog: doy %s -> %s ~ %s" % [w, date_from, date_to])
	_case(s, notes, "plant_while_crop", func(): s.device_command({"action": "plant", "seedling_grade": "B", "plants_per_m2": 8.0}), "심을 수 없어요")
	_case(s, notes, "plant_density_zero", func(): s.device_command({"action": "plant", "seedling_grade": "B", "plants_per_m2": 0.0}))
	_case(s, notes, "plant_unknown_grade", func(): s.device_command({"action": "plant", "seedling_grade": "Z", "plants_per_m2": 8.0}))
	_case(s, notes, "cover_layers_outside_window", func(): s.device_command({"action": "set_cover_layers", "layers": 2}), "%s부터 %s" % [date_from, date_to])
	_case(s, notes, "cover_layers_out_of_range", func(): s.device_command({"action": "set_cover_layers", "layers": 9}), "%d겹" % int(cat["max_cover_layers"]))
	_case(s, notes, "drip_already_installed", func(): s.device_command({"action": "install_drip"}), "이미")
	_case(s, notes, "window_layer_missing", func(): s.device_command({"action": "set_window", "layer": 3, "side": "left", "open_fraction": 0.5}), "3겹")
	_case(s, notes, "window_level_not_allowed", func(): s.device_command({"action": "set_window", "layer": 0, "side": "left", "open_fraction": 0.42}))
	_case(s, notes, "heater_setpoint_high", func(): s.device_command({"action": "set_heater", "on": true, "setpoint_degC": 99.0}), "%d도" % int(cat["heater_setpoint_max_degC"]))
	_case(s, notes, "heater_setpoint_low", func(): s.device_command({"action": "set_heater", "on": true, "setpoint_degC": -5.0}))
	for t in ["watering_can", "fertilizer", "compost", "lime", "disinfectant", "inoculant", "scissors", "predators"]:
		_case(s, notes, "negative_amount_%s" % t, func(): _tool(s, t, 5, -1.0), "0보다")
	_case(s, notes, "unknown_action_fallback", func(): s.device_command({"action": "teleport"}), RejectText.FALLBACK)
	# 자동 물 주기가 거부될 때 (양이 음수): 다음 시간 직전에 SimRunner가 넣다가 거부
	_case(s, notes, "auto_irrigation", func():
		s.set_auto_irrigation(true, s.hour_now(), -1.0)
		s.step_ticks(1)
		s.set_auto_irrigation(true, 8, 4.0), "자동 물 주기")
	s.dispose()
	# 빈 밭: 뽑을 포기 없음
	var e: Object = Driver.new_session("direct_play", false)
	var notes2: Array = []
	e.notice.connect(func(t: String): notes2.append(t))
	_case(e, notes2, "pull_empty_cell", func(): _tool(e, "pull", 7, 0.0), "뽑을 포기")
	e.dispose()
	_ok("doy_to_text", RejectText.doy_to_text(1) == "1월 1일" and RejectText.doy_to_text(91) == "4월 1일" and RejectText.doy_to_text(243) == "8월 31일" and RejectText.doy_to_text(365) == "12월 31일")

	# 캐릭터 화면: 거부를 일으킨 뒤 알림 줄(플레이어 화면)에 영어가 없고, 원문은 개발자 패널 기록에만
	Session.choose_scenario("char_planted")
	var cp: Node = load("res://scenes/char_play.tscn").instantiate()
	get_root().add_child(cp)
	for i in range(3):
		await process_frame
	var cs: Object = cp.s
	cs.device_command({"action": "set_cover_layers", "layers": 2})
	cs.device_command({"action": "plant", "seedling_grade": "B", "plants_per_m2": 8.0})
	cp._process(0.016)
	var note_text: String = cp._note.text
	_ok("player_screen_notice_korean_only", note_text != "" and ascii.search(note_text) == null, note_text.replace("\n", " / "))
	_ok("raw_message_goes_to_dev_panel_notes", cp.dev.raw_notes.size() >= 2 and str(cp.dev.raw_notes[-1]).contains("live crop"), str(cp.dev.raw_notes))
	cp.queue_free()
	await process_frame
	Session.scenario_id = ""
	U.finish(self, fails)
