# 본 화면: HUD, 베드 그리드, 하우스 패널, 행동 패널, 알림, 개발자 패널. UI는 코드로 만들어요(프로토타입).
# 게임 화면은 get_tile_visuals / get_house_controls / get_weather / get_time 등 "보이는 것"만 읽어요.
extends Control

const Cfg = preload("res://scripts/cfg.gd")
const Labels = preload("res://scripts/labels.gd")
const Session = preload("res://scripts/session.gd")
const SaveStore = preload("res://scripts/save_store.gd")
const SimRunner = preload("res://scripts/sim_runner.gd")
const BedView = preload("res://scripts/bed_view.gd")
const DevPanel = preload("res://scripts/dev_panel.gd")

const MONTH_DAYS := [31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31]  # 윤년 없는 달력 (표시용)

var runner: Node
var bed: Control
var dev: PanelContainer
var cat: Dictionary = {}
var _grid := Vector2i(20, 4)
var _last_speed := 2
var _last_tick := -1
var _dirty := true
var _notes: Array = []   # [{text, left}]

var _date_label: Label
var _speed_buttons: Array = []
var _pending_label: Label
var _weather_label: Label
var _harvest_label: Label
var _house_label: Label
var _sel_label: Label
var _auto_check: CheckBox
var _auto_hour: SpinBox
var _auto_mm: SpinBox
var _note_label: Label
var _action_opt: OptionButton
var _form: VBoxContainer
var _fields := {}
var _actions: Array = []
var _end_panel: PanelContainer
var _end_label: Label
var _bench := {"on": false, "frames": 0, "sum_ms": 0.0, "max_ms": 0.0}


func _ready() -> void:
	runner = SimRunner.new()
	runner.name = "SimRunner"
	add_child(runner)
	runner.command_rejected.connect(_on_rejected)
	runner.events_received.connect(_on_events)
	runner.season_ended.connect(_on_season_end)
	runner.ticked.connect(func(): _dirty = true)
	var args := _args()
	var m: String = args.get("mode", Session.mode)
	if not _begin(m):
		return
	_actions = Labels.actions()
	_build_ui()
	_rebuild_form()
	_refresh(true)
	if args.has("autoshot"):
		_autoshot(args)


func _args() -> Dictionary:
	var d := {}
	for a in OS.get_cmdline_user_args():
		var i := a.find("=")
		if i > 0:
			d[a.substr(0, i)] = a.substr(i + 1)
	return d


func _begin(m: String) -> bool:
	var r := {"ok": true}
	var auto_on := bool(Cfg.get_v("auto_irrigation", "enabled"))
	match m:
		Session.MODE_WATCH:
			r = runner.start_new(Cfg.get_v("start", "watch_scenario"), true, false, m)
		Session.MODE_EMPTY:
			r = runner.start_new(Cfg.get_v("start", "empty_scenario"), false, auto_on, m)
		Session.MODE_LOAD:
			r = SaveStore.load_into(runner)
		_:
			r = runner.start_new(Cfg.get_v("start", "direct_scenario"), false, auto_on, Session.MODE_DIRECT)
	if not r["ok"]:
		push_error("시작 실패: %s" % r.get("message", r))
		var l := Label.new()
		l.text = "시작하지 못했어요: %s" % r.get("message", "")
		add_child(l)
		return false
	cat = runner.world.get_catalog()
	_grid = runner.world.get_grid_size()
	return true


# ---------- UI 만들기 ----------
func _lbl(parent: Node, text: String = "") -> Label:
	var l := Label.new()
	l.text = text
	parent.add_child(l)
	return l


func _btn(parent: Node, text: String, cb: Callable) -> Button:
	var b := Button.new()
	b.text = text
	b.focus_mode = Control.FOCUS_NONE
	b.pressed.connect(cb)
	parent.add_child(b)
	return b


func _panel(pos: Vector2, sz: Vector2) -> PanelContainer:
	var p := PanelContainer.new()
	p.position = pos
	p.size = sz
	add_child(p)
	return p


func _build_ui() -> void:
	var bg := ColorRect.new()
	bg.color = Color(0.13, 0.15, 0.13)
	bg.set_anchors_preset(Control.PRESET_FULL_RECT)
	bg.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(bg)

	# 상단 HUD
	var top := _panel(Vector2(8, 6), Vector2(1264, 44))
	var th := HBoxContainer.new()
	th.add_theme_constant_override("separation", 12)
	top.add_child(th)
	_date_label = _lbl(th)
	var names := ["정지", "1시간/초", "6시간/초", "하루/초", "열흘/초"]
	for i in range(names.size()):
		var idx := i
		var b := _btn(th, "%s (%s)" % [names[i], "Space" if i == 0 else str(i)], func(): _set_speed(idx))
		b.toggle_mode = true
		_speed_buttons.append(b)
	_pending_label = _lbl(th)
	_weather_label = _lbl(th)
	_harvest_label = _lbl(th)

	# 베드
	bed = BedView.new()
	bed.position = Vector2(16, 64)
	add_child(bed)
	bed.set_grid(_grid.x, _grid.y)
	bed.selection_changed.connect(_on_selection)
	var bed_h: float = _grid.y * float(Cfg.get_v("visual", "cell_px"))

	# 선택 줄
	var sy := 64.0 + bed_h + 6.0
	var sel := _panel(Vector2(8, sy), Vector2(900, 36))
	var sh := HBoxContainer.new()
	sel.add_child(sh)
	_sel_label = _lbl(sh, "")
	_sel_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_btn(sh, "전체 선택", func(): bed.select_all())
	_btn(sh, "선택 해제", func(): bed.clear_selection())

	# 하우스 패널 + 자동 물 주기 + 저장
	var hy := sy + 44.0
	var hp := _panel(Vector2(8, hy), Vector2(900, 78))
	var hv := VBoxContainer.new()
	hp.add_child(hv)
	_house_label = _lbl(hv)
	var ah := HBoxContainer.new()
	hv.add_child(ah)
	_auto_check = CheckBox.new()
	_auto_check.text = "자동 물 주기"
	_auto_check.focus_mode = Control.FOCUS_NONE
	_auto_check.button_pressed = runner.auto_irrigation["enabled"]
	_auto_check.toggled.connect(func(on: bool): runner.auto_irrigation["enabled"] = on)
	ah.add_child(_auto_check)
	_lbl(ah, " 시각")
	_auto_hour = _spin(ah, 0, 23, 1, float(runner.auto_irrigation["hour"]))
	_auto_hour.value_changed.connect(func(v: float): runner.auto_irrigation["hour"] = int(v))
	_lbl(ah, "시   양")
	_auto_mm = _spin(ah, 0, 100, 0.5, float(runner.auto_irrigation["amount_mm"]))
	_auto_mm.value_changed.connect(func(v: float): runner.auto_irrigation["amount_mm"] = v)
	_lbl(ah, "mm   ")
	_btn(ah, "빠른 저장 (F5)", func(): _quick_save())
	_btn(ah, "빠른 불러오기 (F9)", func(): _quick_load())
	_btn(ah, "개발자 패널 (F3)", func(): _toggle_dev())

	# 행동 패널
	var ap := _panel(Vector2(920, 56), Vector2(352, 500))
	var av := VBoxContainer.new()
	ap.add_child(av)
	_lbl(av, "행동 (영역 행동은 먼저 영역을 고르세요)")
	_action_opt = OptionButton.new()
	_action_opt.focus_mode = Control.FOCUS_NONE
	for a in _actions:
		_action_opt.add_item(a["label"])
	_action_opt.item_selected.connect(func(_i: int): _rebuild_form())
	av.add_child(_action_opt)
	_form = VBoxContainer.new()
	av.add_child(_form)
	var run := _btn(av, "실행", _on_execute)
	run.custom_minimum_size = Vector2(0, 36)
	var hint := _lbl(av, "넣은 명령은 다음 1시간이 시작될 때 적용돼요.\n취소할 수 없어요. 일시정지 중에도 넣을 수 있어요.")
	hint.add_theme_font_size_override("font_size", 11)
	hint.modulate = Color(1, 1, 1, 0.7)

	# 개발자 패널
	var dy := hy + 86.0
	dev = DevPanel.new()
	dev.position = Vector2(8, dy)
	dev.size = Vector2(900, 720 - dy - 34)
	dev.runner = runner
	dev.bed = bed
	add_child(dev)
	dev.visible = bool(Cfg.get_v("visual", "dev_panel_default_on"))

	# 알림 줄
	_note_label = Label.new()
	_note_label.position = Vector2(12, 690)
	_note_label.size = Vector2(1250, 28)
	_note_label.add_theme_color_override("font_color", Color(1, 0.85, 0.5))
	add_child(_note_label)

	# 종료 화면
	_end_panel = _panel(Vector2(380, 220), Vector2(520, 200))
	_end_panel.visible = false
	var ev := VBoxContainer.new()
	_end_panel.add_child(ev)
	_end_label = _lbl(ev)
	var eh := HBoxContainer.new()
	ev.add_child(eh)
	_btn(eh, "계속 진행", func(): _end_panel.visible = false)
	_btn(eh, "시작 화면으로", func(): get_tree().change_scene_to_file("res://scenes/start.tscn"))
	_on_selection([])


func _spin(parent: Node, mn: float, mx: float, st: float, v: float) -> SpinBox:
	var s := SpinBox.new()
	s.min_value = mn
	s.max_value = mx
	s.step = st
	s.value = v
	parent.add_child(s)
	return s


# ---------- 행동 양식 ----------
func _rebuild_form() -> void:
	for c in _form.get_children():
		_form.remove_child(c)
		c.queue_free()
	_fields.clear()
	var a: Dictionary = _actions[_action_opt.selected]
	for f in a["fields"]:
		var row := HBoxContainer.new()
		_form.add_child(row)
		var l := Label.new()
		l.text = f["label"]
		l.custom_minimum_size = Vector2(130, 0)
		row.add_child(l)
		var c: Control
		match f["kind"]:
			"num", "int":
				var sb := SpinBox.new()
				sb.min_value = f.get("min", 0.0)
				sb.max_value = f.get("max", 100.0)
				sb.step = f.get("step", 1.0)
				sb.value = f.get("def", 0.0)
				c = sb
			"bool":
				var cb := CheckBox.new()
				cb.button_pressed = f.get("def", true)
				cb.focus_mode = Control.FOCUS_NONE
				c = cb
			"cat":
				c = _opt_from_ids(Array(cat[f["cat"]]), f.get("def_id", ""))
			"side":
				c = _opt_from_ids(["left", "right"], "")
			"layer":
				var n: int = runner.world.get_house_controls()["cover_layers"]
				var ids := []
				for i in range(n):
					ids.append(str(i))
				c = _opt_from_ids(ids, "", true)
			"levels":
				var ids2 := []
				for v in cat["window_open_levels"]:
					ids2.append("%.2f" % v)
				c = _opt_from_ids(ids2, "", true)
		c.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		row.add_child(c)
		_fields[f["key"]] = c


func _opt_from_ids(ids: Array, def_id: String, raw := false) -> OptionButton:
	var o := OptionButton.new()
	o.focus_mode = Control.FOCUS_NONE
	for id in ids:
		o.add_item(str(id) if raw else Labels.name_of(str(id)))
		o.set_item_metadata(o.item_count - 1, str(id))
		if str(id) == def_id:
			o.select(o.item_count - 1)
	return o


func build_command(a: Dictionary) -> Dictionary:
	var cmd := {"action": a["id"]}
	if a["area"]:
		cmd["area"] = bed.selection.duplicate()
	for f in a["fields"]:
		var c: Control = _fields[f["key"]]
		match f["kind"]:
			"num":
				cmd[f["key"]] = float((c as SpinBox).value)
			"int":
				cmd[f["key"]] = int((c as SpinBox).value)
			"bool":
				cmd[f["key"]] = (c as CheckBox).button_pressed
			"cat", "side":
				var o := c as OptionButton
				cmd[f["key"]] = str(o.get_item_metadata(o.selected))
			"layer":
				var o2 := c as OptionButton
				cmd[f["key"]] = int(str(o2.get_item_metadata(o2.selected)))
			"levels":
				var o3 := c as OptionButton
				cmd[f["key"]] = float(str(o3.get_item_metadata(o3.selected)))
	return cmd


func _on_execute() -> void:
	var a: Dictionary = _actions[_action_opt.selected]
	if a["area"] and bed.selection.is_empty():
		_notify("먼저 영역을 고르세요 (끌어서 사각형, 또는 전체 선택).")
		return
	var cmd := build_command(a)
	var r: Dictionary = runner.submit(cmd)
	if r["ok"]:
		_notify("명령을 넣었어요: %s (다음 1시간이 시작될 때 적용)" % a["label"])
	_dirty = true


# ---------- 알림·이벤트 ----------
func _notify(text: String) -> void:
	_notes.append({"text": text, "left": float(Cfg.get_v("visual", "notification_seconds"))})
	if _notes.size() > 3:
		_notes.pop_front()


func _on_rejected(message: String, _cmd: Dictionary) -> void:
	_notify("명령이 거부됐어요 (게임은 계속돼요): %s" % message)


func _on_events(events: Array) -> void:
	for e in events:
		if e["type"] == "fruit_harvested":
			bed.flash(int(e["tile_index"]))


func _on_season_end(s: Dictionary) -> void:
	var h: Dictionary = s["harvest"]
	_end_label.text = "작기가 끝났어요 (tick %d)\n수확 %d개, %.1f kg\n최종 해시 %s\n(계속 진행도 할 수 있어요)" % [
		s["tick"], h["harvested_fruits"], float(h["harvested_fresh_g"]) / 1000.0, s["state_hash"]]
	_end_panel.visible = true
	_set_speed(0)


func _on_selection(area: Array) -> void:
	if area.is_empty():
		_sel_label.text = "선택: 없음 (왼쪽 끌기 = 영역, 한 번 누르기 = 1칸, 오른쪽 누르기 = 해제)"
	else:
		_sel_label.text = "선택: 행 %d, 열 %d부터 %d × %d칸" % [area[0], area[1], area[2], area[3]]
		if dev != null:
			dev.cell_index = area[0] * _grid.x + area[1]


# ---------- 입력 ----------
func _set_speed(i: int) -> void:
	if i > 0:
		_last_speed = i
	runner.set_speed(i)
	_dirty = true


func _toggle_dev() -> void:
	dev.visible = not dev.visible


func _quick_save() -> void:
	var r := SaveStore.quick_save(runner)
	_notify(("저장했어요 (tick %d)" % runner.world.get_tick()) if r["ok"] else ("저장 실패: %s" % r["message"]))


func _quick_load() -> void:
	var r := SaveStore.load_into(runner)
	if r["ok"]:
		_auto_check.set_pressed_no_signal(runner.auto_irrigation["enabled"])
		_auto_hour.set_value_no_signal(runner.auto_irrigation["hour"])
		_auto_mm.set_value_no_signal(runner.auto_irrigation["amount_mm"])
		_notify("불러왔어요 (tick %d, 일시정지)" % r["tick"])
		_end_panel.visible = false
		_last_tick = -1
		_dirty = true
	else:
		_notify(r["message"])


func _input(ev: InputEvent) -> void:
	if not (ev is InputEventKey) or not ev.pressed or ev.echo:
		return
	var f := get_viewport().gui_get_focus_owner()
	var typing := f is LineEdit
	match ev.keycode:
		KEY_F3:
			_toggle_dev()
		KEY_F5:
			_quick_save()
		KEY_F9:
			_quick_load()
		KEY_SPACE:
			if not typing:
				_set_speed(_last_speed if runner.speed_index == 0 else 0)
				get_viewport().set_input_as_handled()
		KEY_1, KEY_2, KEY_3, KEY_4:
			if not typing:
				_set_speed(ev.keycode - KEY_0)
				get_viewport().set_input_as_handled()


# ---------- 매 프레임 ----------
func _process(delta: float) -> void:
	for n in _notes:
		n["left"] -= delta
	_notes = _notes.filter(func(n): return n["left"] > 0.0)
	var lines := PackedStringArray()
	for n in _notes:
		lines.push_back(n["text"])
	_note_label.text = "\n".join(lines)
	_note_label.position.y = 718.0 - 22.0 * maxi(lines.size(), 1)
	if _bench["on"]:
		var ms := delta * 1000.0
		_bench["frames"] += 1
		_bench["sum_ms"] += ms
		_bench["max_ms"] = maxf(_bench["max_ms"], ms)
	_refresh(false)


func _refresh(force: bool) -> void:
	var w: Object = runner.world
	var t: Dictionary = w.get_time()
	if force or _dirty or int(t["tick"]) != _last_tick:
		_last_tick = int(t["tick"])
		_dirty = false
		bed.set_visuals(w.get_tile_visuals(float(Cfg.get_v("visual", "ripening_min_index"))))
		var doy: int = t["day_of_year"]
		var mo := 0
		while mo < 11 and doy > MONTH_DAYS[mo]:
			doy -= MONTH_DAYS[mo]
			mo += 1
		_date_label.text = "%d월 %d일 %02d시 (경과 %d일)" % [mo + 1, doy, t["hour"], t["elapsed_days"]]
		var wx: Dictionary = w.get_weather()
		_weather_label.text = "바깥 %.1f°C · 일사 %.0f W/m² · 습도 %.0f%%" % [
			wx["outside_air_temp_degC"], wx["solar_radiation_W_per_m2"], wx["outside_rh_percent"]]
		var tot: Dictionary = w.get_harvest_totals()
		_harvest_label.text = "수확 %d개 %.1f kg" % [tot["harvested_fruits"], float(tot["harvested_fresh_g"]) / 1000.0]
		var hc: Dictionary = w.get_house_controls()
		_house_label.text = "하우스: 피복 %d겹 · 열린 창 %d개 · 난방 %s · 팬 %s · 점적 %s" % [
			hc["cover_layers"], hc["windows_open_count"],
			("켜짐 (%.0f°C)" % hc["heater_setpoint_degC"]) if hc["heater_on"] else "꺼짐",
			"켜짐" if hc["fan_on"] else "꺼짐", "설치됨" if hc["drip_installed"] else "없음"]
	_pending_label.text = "대기 명령 %d개" % w.get_pending_command_count()
	for i in range(_speed_buttons.size()):
		_speed_buttons[i].set_pressed_no_signal(i == runner.speed_index)


# ---------- 자동 화면 캡처 (명령줄: -- autoshot=<폴더> [mode=direct|empty|watch]) ----------
func _shot(dir: String, file_name: String) -> void:
	await RenderingServer.frame_post_draw
	var img := get_viewport().get_texture().get_image()
	var path := dir.path_join(file_name)
	var e := img.save_png(path)
	print("shot %s (%s) size=%s" % [path, "ok" if e == OK else "err %d" % e, img.get_size()])


func _autoshot(args: Dictionary) -> void:
	var dir: String = args["autoshot"]
	DirAccess.make_dir_recursive_absolute(dir)
	dev.visible = false
	await get_tree().process_frame
	await get_tree().process_frame
	await _shot(dir, "p23_1_start.png")
	# 입력 자체 검사: 합성 마우스·키 입력을 뷰포트에 밀어 넣어요 (사람 손 없이 H4·H9 일부 확인)
	_press_mouse(bed.position + Vector2(5, 5), true)
	_press_mouse(bed.position + Vector2(44 * 3 + 5, 44 + 5), false)
	await get_tree().process_frame
	print("input_test drag_select=%s (expect [0, 0, 2, 4])" % [bed.selection])
	_press_mouse(bed.position + Vector2(44 * 7 + 5, 44 * 2 + 5), true)
	_press_mouse(bed.position + Vector2(44 * 7 + 5, 44 * 2 + 5), false)
	print("input_test click_select=%s (expect [2, 7, 1, 1])" % [bed.selection])
	bed.select_all()
	print("input_test select_all=%s (expect [0, 0, 4, 20])" % [bed.selection])
	for k in [KEY_3, KEY_SPACE, KEY_SPACE, KEY_4, KEY_SPACE, KEY_1]:
		_press_key(k)
		print("input_test key=%s speed_index=%d" % [OS.get_keycode_string(k), runner.speed_index])
	_press_key(KEY_SPACE)
	_press_key(KEY_F5)
	_press_key(KEY_F9)
	print("input_test f5_f9 notes=%s speed_index=%d" % [_notes.map(func(n): return n["text"]), runner.speed_index])
	# 선택 + 명령 몇 개 (물, 비료, 창, 난방) — 대기 명령 수가 HUD에 보이게
	bed.selection = [0, 0, 2, 6]
	bed.selection_changed.emit(bed.selection)
	for cmd in [
		{"action": "irrigate", "area": [0, 0, 2, 6], "amount_mm": 3.0},
		{"action": "add_fertilizer", "area": [0, 0, 2, 6], "fertilizer": "calcium_nitrate", "g_per_m2": 4.0},
		{"action": "set_window", "layer": 0, "side": "left", "open_fraction": 0.5},
		{"action": "set_heater", "on": true, "setpoint_degC": 8.0},
	]:
		runner.submit(cmd)
	# 거부 알림 확인: 작물이 있을 때 정식 (empty 모드에서는 받아들여져요)
	runner.submit({"action": "plant", "seedling_grade": "B", "plants_per_m2": 8.0})
	runner.set_speed(0)
	runner.step(24 * 120)  # 120일
	_dirty = true
	await get_tree().process_frame
	await _shot(dir, "p23_2_midseason.png")
	dev.visible = true
	dev.cell_index = 21
	bed.selection = [1, 1, 1, 1]
	bed.selection_changed.emit(bed.selection)
	await get_tree().create_timer(0.5).timeout
	await _shot(dir, "p23_3_devpanel.png")
	# 가장 빠른 배속(열흘 = 1초)에서 프레임 시간
	dev.visible = false
	_bench["on"] = true
	_bench["frames"] = 0
	_bench["sum_ms"] = 0.0
	_bench["max_ms"] = 0.0
	runner.stats["step_usec_sum"] = 0
	runner.stats["step_usec_max"] = 0
	runner.stats["frames"] = 0
	runner.stats["capped_frames"] = 0
	runner.stats["ticks_run"] = 0
	_set_speed(4)
	await get_tree().create_timer(5.0).timeout
	_set_speed(0)
	_bench["on"] = false
	var st: Dictionary = runner.stats
	var sim_frames := maxf(1.0, float(st["frames"]))
	print("bench speed=4 frames=%d mean_frame_ms=%.2f max_frame_ms=%.2f capped_frames=%d ticks=%d" % [
		_bench["frames"], _bench["sum_ms"] / maxf(1, _bench["frames"]), _bench["max_ms"], st["capped_frames"], st["ticks_run"]])
	print("bench sim_ms_per_frame mean=%.3f max=%.3f (advance + auto irrigation + events)" % [
		float(st.get("step_usec_sum", 0)) / 1000.0 / sim_frames, float(st.get("step_usec_max", 0)) / 1000.0])
	print("final tick=%d" % runner.world.get_tick())
	get_tree().quit()


func _press_mouse(pos: Vector2, pressed: bool) -> void:
	var ev := InputEventMouseButton.new()
	ev.button_index = MOUSE_BUTTON_LEFT
	ev.pressed = pressed
	ev.position = pos
	ev.global_position = pos
	get_viewport().push_input(ev)


func _press_key(code: Key) -> void:
	var ev := InputEventKey.new()
	ev.keycode = code
	ev.pressed = true
	_input(ev)
