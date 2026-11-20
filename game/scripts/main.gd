# 점검 화면(본 화면): HUD, 베드 그리드, 하우스 줄, 행동 패널, 알림, 저장 목록, 작기 종료 화면, 개발자 패널.
# UI는 코드로 만들어요. 게임 화면은 get_tile_visuals / get_house_controls / get_weather / get_time / get_harvest_totals 같은
# "보이는 것"만 읽어요. 진짜 값(get_true_*)은 개발자 패널(dev_panel.gd)만 읽어요 (결정 D1, E8).
extends Control

const Cfg = preload("res://scripts/cfg.gd")
const Session = preload("res://scripts/session.gd")
const SaveStore = preload("res://scripts/save_store.gd")
const SimRunner = preload("res://scripts/sim_runner.gd")
const BedView = preload("res://scripts/bed_view.gd")
const ActionPanel = preload("res://scripts/action_panel.gd")
const SavePanel = preload("res://scripts/save_panel.gd")
const DevPanel = preload("res://scripts/dev_panel.gd")
const Labels = preload("res://scripts/labels.gd")

const START_SCENE := "res://scenes/start.tscn"
const DEFAULT_SCENARIO := "direct_play"
const NOTE_MAX := 3
const NOTE_LINE_PX := 26.0

var runner: Node
var bed: Control
var dev: PanelContainer
var action_panel: PanelContainer
var save_panel: PanelContainer
var end_panel: PanelContainer

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
var _end_label: Label
var _ripening_min := 0.8


func _ready() -> void:
	runner = SimRunner.new()
	runner.name = "SimRunner"
	add_child(runner)
	runner.command_rejected.connect(_on_rejected)
	runner.events_received.connect(_on_events)
	runner.season_ended.connect(_on_season_end)
	runner.autosaved.connect(_on_autosaved)
	runner.ticked.connect(func(): _dirty = true)
	_ripening_min = float(Cfg.get_v("visual", "ripening_min_index"))
	var err := _begin()
	if err != "":
		_show_start_error(err)
		return
	_grid = runner.world.get_grid_size()
	_build_ui()
	runner.world_replaced.connect(_on_world_replaced)
	_sync_after_world_change()
	_refresh(true)


# 새 판 또는 불러오기. 실패하면 이유(한국어)를 돌려줘요.
func _begin() -> String:
	if Session.load_id != "":
		var id := Session.load_id
		Session.load_id = ""
		var r := SaveStore.load_into(runner, id)
		return "" if r["ok"] else str(r["message"])
	var entry := Cfg.scenario(Session.scenario_id if Session.scenario_id != "" else DEFAULT_SCENARIO)
	if entry.is_empty():
		return "시나리오 항목을 찾을 수 없어요: %s" % Session.scenario_id
	var r2: Dictionary = runner.start_new(entry)
	return "" if r2["ok"] else "시작하지 못했어요: %s" % r2.get("message", "")


func _show_start_error(message: String) -> void:
	push_error("시작 실패: %s" % message)
	var bg := ColorRect.new()
	bg.color = Cfg.color("background")
	bg.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(bg)
	var v := VBoxContainer.new()
	v.position = Vector2(60, 120)
	v.custom_minimum_size = Vector2(900, 0)
	add_child(v)
	var l := Label.new()
	l.text = message
	l.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	l.custom_minimum_size = Vector2(900, 0)
	l.add_theme_color_override("font_color", Cfg.color("warning"))
	v.add_child(l)
	_btn(v, "시작 화면으로", func(): get_tree().change_scene_to_file(START_SCENE))


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


func _spin(parent: Node, input_key: String, v: float) -> SpinBox:
	var r := Cfg.input(input_key)
	var s := SpinBox.new()
	s.min_value = r["min"]
	s.max_value = r["max"]
	s.step = r["step"]
	s.value = v
	parent.add_child(s)
	return s


func _build_ui() -> void:
	var bg := ColorRect.new()
	bg.color = Cfg.color("background")
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
	for i in range(runner.speed_count()):
		var idx := i
		var label: String = names[i] if i < names.size() else "x%d" % i
		var b := _btn(th, "%s (%s)" % [label, "Space" if i == 0 else str(i)], func(): _set_speed(idx))
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

	# 하우스 줄 + 자동 물 주기 + 저장
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
	_auto_check.toggled.connect(func(_on: bool): _apply_auto())
	ah.add_child(_auto_check)
	_lbl(ah, " 시각")
	_auto_hour = _spin(ah, "auto_hour", 8.0)
	_auto_hour.value_changed.connect(func(_v: float): _apply_auto())
	_lbl(ah, "시   양")
	_auto_mm = _spin(ah, "auto_mm", 4.0)
	_auto_mm.value_changed.connect(func(_v: float): _apply_auto())
	_lbl(ah, "mm   ")
	_btn(ah, "저장·불러오기", func(): _toggle_save_panel())
	_btn(ah, "개발자 패널 (F3)", func(): _toggle_dev())

	# 행동 패널
	action_panel = ActionPanel.new()
	action_panel.position = Vector2(920, 56)
	action_panel.size = Vector2(352, 500)
	add_child(action_panel)
	action_panel.setup(runner, bed)
	action_panel.submitted.connect(_on_action_submitted)
	action_panel.area_missing.connect(func(_a: Dictionary): _notify("먼저 영역을 고르세요 (끌어서 사각형, 또는 전체 선택)."))

	# 개발자 패널
	var dy := hy + 86.0
	dev = DevPanel.new()
	dev.position = Vector2(8, dy)
	dev.size = Vector2(900, 720 - dy - NOTE_MAX * NOTE_LINE_PX)  # 아래 알림 자리를 남겨요
	add_child(dev)
	dev.setup(runner, bed)
	dev.visible = bool(Cfg.get_v("visual", "dev_panel_default_on"))

	# 알림 줄
	_note_label = Label.new()
	_note_label.position = Vector2(12, 690)
	_note_label.size = Vector2(1250, 28)
	_note_label.add_theme_color_override("font_color", Cfg.color("notice"))
	add_child(_note_label)

	# 저장·불러오기 (겹쳐 뜨는 목록)
	save_panel = SavePanel.new()
	save_panel.position = Vector2(200, 60)
	save_panel.size = Vector2(880, 560)
	add_child(save_panel)
	save_panel.setup(true, true)
	save_panel.add_theme_stylebox_override("panel", Cfg.overlay_style())
	save_panel.visible = false
	save_panel.save_requested.connect(_on_save_requested)
	save_panel.load_requested.connect(_on_load_requested)
	save_panel.closed.connect(func(): save_panel.visible = false)

	# 작기 종료 화면
	end_panel = _panel(Vector2(380, 200), Vector2(520, 220))
	end_panel.visible = false
	end_panel.add_theme_stylebox_override("panel", Cfg.overlay_style())
	var ev := VBoxContainer.new()
	end_panel.add_child(ev)
	_end_label = _lbl(ev)
	_end_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_end_label.custom_minimum_size = Vector2(500, 0)
	var eh := HBoxContainer.new()
	ev.add_child(eh)
	_btn(eh, "계속 진행", func(): end_panel.visible = false)
	_btn(eh, "시작 화면으로", func(): get_tree().change_scene_to_file(START_SCENE))
	_on_selection([])


# 월드가 바뀐 뒤(새 판·불러오기) 화면 요소를 새 상태에 맞춰요.
func _sync_after_world_change() -> void:
	if _auto_check == null:
		return
	var a: Dictionary = runner.auto_irrigation
	_auto_check.set_pressed_no_signal(bool(a["enabled"]))
	_auto_hour.set_value_no_signal(float(a["hour"]))
	_auto_mm.set_value_no_signal(float(a["amount_mm"]))
	action_panel.rebind()
	dev.rebind()
	bed.clear_selection()
	_last_tick = -1
	_dirty = true
	end_panel.visible = false


func _on_world_replaced() -> void:
	_sync_after_world_change()


# 자동 물 주기 설정: 바꾸는 즉시 따라요 (F3). 러너가 틱마다 설정을 읽어요.
func _apply_auto() -> void:
	runner.set_auto_irrigation(_auto_check.button_pressed, int(_auto_hour.value), float(_auto_mm.value))


# ---------- 알림·이벤트 ----------
func _notify(text: String) -> void:
	_notes.append({"text": text, "left": float(Cfg.get_v("visual", "notification_seconds"))})
	while _notes.size() > NOTE_MAX:
		_notes.pop_front()


func _on_rejected(message: String, cmd: Dictionary) -> void:
	var summary := Labels.reject_summary(str(cmd.get("action", "")))
	_notify("명령이 거부됐어요 (게임은 계속돼요): %s (%s)" % [summary, message])


func _on_action_submitted(a: Dictionary, r: Dictionary) -> void:
	if r["ok"]:
		_notify("명령을 넣었어요: %s (다음 1시간이 시작될 때 적용)" % a["label"])
	_dirty = true


func _on_autosaved(r: Dictionary) -> void:
	if not r["ok"]:
		_notify("자동 저장에 실패했어요: %s" % r["message"])


func _on_events(events: Array) -> void:
	var died: Array = []
	for e in events:
		match e["type"]:
			"fruit_harvested":
				bed.flash(int(e["tile_index"]))
			"plant_died":
				died.append(int(e["tile_index"]))
	# 게임 화면은 "죽었다"만 알려요. 원인(cause)은 개발자 패널에만 있어요 (결정 F4)
	if died.size() == 1:
		_notify("행 %d, 열 %d의 포기가 죽었어요." % [died[0] / _grid.x, died[0] % _grid.x])
	elif died.size() > 1:
		_notify("포기 %d개가 죽었어요." % died.size())


func _on_season_end(s: Dictionary) -> void:
	var h: Dictionary = s["harvest"]
	var t: Dictionary = s["time"]
	_end_label.text = "작기가 끝났어요 (%d월 %d일, tick %d)\n수확 %d개, %.1f kg (기형과 %d개)\n최종 해시 %s\n자동 저장 칸에 저장했어요. 계속 진행도 할 수 있어요." % [
		t["month"], t["day"], s["tick"], h["harvested_fruits"], float(h["harvested_fresh_g"]) / 1000.0,
		h["harvested_deformed"], s["state_hash"]]
	end_panel.visible = true
	_dirty = true


func _on_selection(area: Array) -> void:
	if area.is_empty():
		_sel_label.text = "선택: 없음 (왼쪽 끌기 = 영역, 한 번 누르기 = 1칸, 오른쪽 누르기 = 해제)"
	else:
		_sel_label.text = "선택: 행 %d, 열 %d부터 %d × %d칸" % [area[0], area[1], area[2], area[3]]
	if action_panel != null:
		action_panel.mark_dirty()


# ---------- 입력 ----------
func _set_speed(i: int) -> void:
	if i > 0:
		_last_speed = i
	runner.set_speed(i)
	_dirty = true


func _toggle_dev() -> void:
	dev.visible = not dev.visible


func _toggle_save_panel() -> void:
	if not save_panel.visible:
		save_panel.refresh()
	save_panel.visible = not save_panel.visible


func _quick_save() -> void:
	_save_to(SaveStore.QUICK)


func _on_save_requested(id: String) -> void:
	_save_to(id)
	save_panel.refresh()


func _save_to(id: String) -> void:
	var r := SaveStore.save(runner, id)
	if not r["ok"]:
		_notify("저장 실패: %s" % r["message"])
		return
	var text := "저장했어요 (%s, tick %d)" % [SaveStore.slot_label(id), runner.world.get_tick()]
	if r["preserved"] != "":
		text += " — 이 칸의 옛 파일은 %s로 남겨 두었어요" % (str(r["preserved"]) + SaveStore.EXT)
	_notify(text)


func _quick_load() -> void:
	_load_from(SaveStore.QUICK)


func _on_load_requested(id: String) -> void:
	_load_from(id)


func _load_from(id: String) -> void:
	var r := SaveStore.load_into(runner, id)
	if r["ok"]:
		save_panel.visible = false
		_notify("불러왔어요 (%s, tick %d, 일시정지)" % [SaveStore.slot_label(id), r["tick"]])
	else:
		_notify(str(r["message"]))  # 다른 버전·깨진 세이브: 이유를 알리고 지금 판은 그대로 (D6)


func _input(ev: InputEvent) -> void:
	if save_panel == null or not (ev is InputEventKey) or not ev.pressed or ev.echo:
		return
	var typing := get_viewport().gui_get_focus_owner() is LineEdit
	match ev.keycode:
		KEY_F3:
			_toggle_dev()
		KEY_F5:
			_quick_save()
		KEY_F9:
			_quick_load()
		KEY_ESCAPE:
			save_panel.visible = false
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
	if runner == null or runner.world == null or _note_label == null:
		return
	for n in _notes:
		n["left"] -= delta
	_notes = _notes.filter(func(n): return n["left"] > 0.0)
	var lines := PackedStringArray()
	for n in _notes:
		lines.push_back(n["text"])
	_note_label.text = "\n".join(lines)
	_note_label.position.y = 716.0 - NOTE_LINE_PX * maxi(lines.size(), 1)
	_refresh(false)


func _refresh(force: bool) -> void:
	var w: Object = runner.world
	var t: Dictionary = w.get_time()
	if force or _dirty or int(t["tick"]) != _last_tick:
		_last_tick = int(t["tick"])
		_dirty = false
		bed.set_visuals(w.get_tile_visuals(_ripening_min))
		_date_label.text = "%d월 %d일 %02d시 (%d일 경과)" % [t["month"], t["day"], t["hour"], int(t["elapsed_days"])]
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
