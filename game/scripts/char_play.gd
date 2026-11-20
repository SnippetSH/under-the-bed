# 캐릭터 플레이 화면 (M6b, 명세 2~8장). 입력 → CharSession, 그리기 → HouseView3D, HUD·작은 창은 여기서.
# 게임 화면이라 진짜 값 접근자(get_true 계열)를 부르지 않아요(개발자 패널 F3만 진짜 값을 보여 줘요. E8·C7 검사 대상).
# 시작 화면이 Session에 남긴 판(scenario_id) 또는 저장 칸(load_id)으로 시작해요.
extends Node

const Cfg = preload("res://scripts/cfg.gd")
const Labels = preload("res://scripts/labels.gd")
const Session = preload("res://scripts/session.gd")
const SaveStore = preload("res://scripts/save_store.gd")
const CharSession = preload("res://scripts/char_session.gd")
const Tools = preload("res://scripts/char_tools.gd")
const HouseView3D = preload("res://scripts/house_view_3d.gd")
const DevPanel = preload("res://scripts/dev_panel.gd")
const Soft = preload("res://scripts/soft_art.gd")
const Lib = preload("res://scripts/art_library.gd")
const ToolbarState = preload("res://scripts/toolbar_state.gd")
const HelpData = preload("res://scripts/help_data.gd")
const ActionNames = preload("res://scripts/action_names.gd")
const CellInfo = preload("res://scripts/cell_info.gd")

const START_SCENE := "res://scenes/start.tscn"
const DEFAULT_SCENARIO := "direct_play"
const TOOL_KEYS := [KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9, KEY_0, KEY_MINUS]
const TOOL_KEY_NAMES := ["1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-"]

var s: Object                 # CharSession
var view: Node3D
var hud: CanvasLayer
var dev: PanelContainer
var _tint: ColorRect
var _root: Control
var _sky_icon: TextureRect
var _speed_buttons: Array = []
var _speed_label: Label
var _skip_btn: Button
var _tool_pill: PanelContainer
var _slot_on: StyleBoxFlat
var _slot_off: StyleBoxFlat
var _date: Label
var _state: Label
var _pending: Label
var _harvest: Label
var _tool_label: Label
var _slots: Array = []
var _note: Label
var _notes: Array = []
var _popup: PanelContainer
var _popup_box: VBoxContainer
var _popup_kind := ""
var _last_tick := -1
var _season_done := false
# HUD 크기: game_config [ui] ui_scale. 캐릭터 화면에서는 창 늘이기(stretch)를 끄고 실제 화면 픽셀로 그려요.
# TODO(M6b UI 크기, 명세 2.3·Q4·C12): UI를 화면 높이에 비례시키는 일은 다음 작업이에요. 지금은 프로토타입대로 고정 배율이에요.
var K := 1.0
var _prev_scale_mode := -1
var _bar_card: PanelContainer
var _clock_card: PanelContainer
var _speed_card: PanelContainer
var _info_card: PanelContainer
var _hint: Label
var _cell_chip: PanelContainer
var _cell_label: Label
var _cell_key := ""             # 칸 정보를 다시 쓸지 (대상 칸·틱이 바뀔 때만)
var bar := ToolbarState.new()   # 도구 줄 숨김·잠깐 보이기·고정 (명세 2.3)


func _ready() -> void:
	K = float(Cfg.get_v("ui", "ui_scale"))
	var win := get_window()
	_prev_scale_mode = win.content_scale_mode
	win.content_scale_mode = Window.CONTENT_SCALE_MODE_DISABLED   # 점검 화면은 그대로 (나갈 때 되돌려요)
	s = CharSession.new()
	s.notice.connect(_notify)
	s.device_opened.connect(_open_device)
	s.season_ended.connect(_on_season_end)
	s.mode_changed.connect(func(_m: String): _last_tick = -1)
	var err := _begin()
	if err != "":
		_show_start_error(err)
		return
	view = HouseView3D.new()
	add_child(view)
	view.setup(s)
	_build_hud()
	bar.setup(s.toolbar_shown, float(Cfg.get_v("ui", "toolbar_slide_seconds")), float(Cfg.get_v("ui", "toolbar_peek_seconds")))
	_refresh(true)


# 새 판 또는 불러오기 (시작 화면이 Session에 남긴 값). 실패하면 이유(한국어 문장)를 돌려줘요
func _begin() -> String:
	if Session.load_id != "":
		var id := Session.load_id
		Session.load_id = ""
		var r: Dictionary = s.load_slot(id)
		if not r["ok"]:
			s.raw_rejections.append("불러오기 실패: %s" % r.get("message", ""))
			return _load_fail_text(r)
		return ""
	var entry := Cfg.scenario(Session.scenario_id if Session.scenario_id != "" else DEFAULT_SCENARIO)
	if entry.is_empty():
		return "시작할 판을 찾을 수 없어요."
	var r2: Dictionary = s.start(entry)
	if not r2["ok"]:
		s.raw_rejections.append("시작 실패: %s" % r2.get("message", ""))
		return "판을 시작하지 못했어요."
	return ""


func _show_start_error(message: String) -> void:
	push_warning("캐릭터 화면 시작 실패: %s %s" % [message, s.raw_rejections])
	var c := Control.new()
	c.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(c)
	var v := VBoxContainer.new()
	v.position = Vector2(60, 120)
	c.add_child(v)
	var l := Label.new()
	l.text = message
	v.add_child(l)
	var b := Button.new()
	b.text = "시작 화면으로"
	b.pressed.connect(func(): get_tree().change_scene_to_file(START_SCENE))
	v.add_child(b)


func _exit_tree() -> void:
	if _prev_scale_mode >= 0:
		get_window().content_scale_mode = _prev_scale_mode
	if s != null:
		s.dispose()


# ---------- HUD (둥근 판, 따뜻한 색, 도구 그림, 속도 단추, 1시간 건너뛰기) ----------
const C_PANEL := Color(0.18, 0.14, 0.11, 0.78)
const C_PANEL_BORDER := Color(1.0, 0.92, 0.78, 0.18)
const C_TEXT := Color(1.0, 0.97, 0.9)
const C_DIM := Color(1.0, 0.95, 0.85, 0.62)
const C_ACCENT := Color(1.0, 0.84, 0.45)


# 설계 픽셀(1280×720 기준 값) → 지금 화면 픽셀
func _k(v: float) -> int:
	return maxi(1, int(round(v * K)))


func _style(bg: Color, radius := 14, border := C_PANEL_BORDER, pad := 10) -> StyleBoxFlat:
	var sb := StyleBoxFlat.new()
	sb.bg_color = bg
	sb.set_corner_radius_all(_k(radius))
	sb.set_border_width_all(1)
	sb.border_color = border
	sb.content_margin_left = _k(pad)
	sb.content_margin_right = _k(pad)
	sb.content_margin_top = _k(pad * 0.6)
	sb.content_margin_bottom = _k(pad * 0.6)
	sb.shadow_color = Color(0, 0, 0, 0.22)
	sb.shadow_size = _k(6)
	sb.shadow_offset = Vector2(0, _k(2))
	sb.anti_aliasing = true
	return sb


func _make_theme() -> Theme:
	var th := Theme.new()
	th.default_font = HouseView3D._font()
	th.default_font_size = _k(16)
	th.set_stylebox("panel", "PanelContainer", _style(C_PANEL))
	th.set_color("font_color", "Label", C_TEXT)
	th.set_color("font_outline_color", "Label", Color(0.15, 0.1, 0.06, 0.8))
	th.set_constant("outline_size", "Label", _k(4))
	th.set_stylebox("normal", "Button", _style(Color(1.0, 0.95, 0.85, 0.14), 10, Color(1, 1, 1, 0.12), 10))
	th.set_stylebox("hover", "Button", _style(Color(1.0, 0.92, 0.75, 0.26), 10, Color(1, 1, 1, 0.25), 10))
	th.set_stylebox("pressed", "Button", _style(Color(1.0, 0.8, 0.4, 0.55), 10, C_ACCENT, 10))
	th.set_stylebox("hover_pressed", "Button", _style(Color(1.0, 0.8, 0.4, 0.62), 10, C_ACCENT, 10))
	th.set_stylebox("disabled", "Button", _style(Color(1, 1, 1, 0.05), 10, Color(1, 1, 1, 0.05), 10))
	th.set_color("font_color", "Button", C_TEXT)
	th.set_color("font_pressed_color", "Button", Color(0.2, 0.12, 0.05))
	th.set_color("font_hover_pressed_color", "Button", Color(0.2, 0.12, 0.05))
	th.set_color("font_disabled_color", "Button", Color(1, 1, 1, 0.35))
	th.set_color("font_color", "CheckBox", C_TEXT)
	var track := StyleBoxFlat.new()
	track.bg_color = Color(1, 0.95, 0.85, 0.08)
	track.set_corner_radius_all(_k(4))
	track.content_margin_left = _k(3)
	track.content_margin_right = _k(3)
	var grab := StyleBoxFlat.new()
	grab.bg_color = Color(1.0, 0.86, 0.55, 0.55)
	grab.set_corner_radius_all(_k(4))
	grab.content_margin_left = _k(3)
	grab.content_margin_right = _k(3)
	var grab_hi := grab.duplicate()
	grab_hi.bg_color = Color(1.0, 0.86, 0.55, 0.85)
	th.set_stylebox("scroll", "VScrollBar", track)
	th.set_stylebox("scroll_focus", "VScrollBar", track)
	th.set_stylebox("grabber", "VScrollBar", grab)
	th.set_stylebox("grabber_highlight", "VScrollBar", grab_hi)
	th.set_stylebox("grabber_pressed", "VScrollBar", grab_hi)
	th.set_icon("increment", "VScrollBar", ImageTexture.new())
	th.set_icon("decrement", "VScrollBar", ImageTexture.new())
	return th


func _lbl(parent: Node, text := "", size := 16, col := C_TEXT) -> Label:
	var l := Label.new()
	l.text = text
	l.add_theme_font_size_override("font_size", _k(size))
	l.add_theme_color_override("font_color", col)
	parent.add_child(l)
	return l


func _btn(parent: Node, text: String, cb: Callable) -> Button:
	var b := Button.new()
	b.text = text
	b.focus_mode = Control.FOCUS_NONE
	b.pressed.connect(cb)
	parent.add_child(b)
	return b


func _card(parent: Node, pos: Vector2) -> PanelContainer:
	var p := PanelContainer.new()
	p.position = pos
	parent.add_child(p)
	return p


func _build_hud() -> void:
	# 시각에 따른 빛 (곱하기 색) + 가장자리 어둡게: 3D 바로 위, HUD 아래 (꾸밈, 시각만 봐요)
	var tint_layer := CanvasLayer.new()
	tint_layer.layer = 1
	add_child(tint_layer)
	_tint = ColorRect.new()
	_tint.set_anchors_preset(Control.PRESET_FULL_RECT)
	_tint.mouse_filter = Control.MOUSE_FILTER_IGNORE
	var mm := CanvasItemMaterial.new()
	mm.blend_mode = CanvasItemMaterial.BLEND_MODE_MUL
	_tint.material = mm
	tint_layer.add_child(_tint)
	var vig := TextureRect.new()
	vig.texture = Soft.vignette_texture()
	vig.set_anchors_preset(Control.PRESET_FULL_RECT)
	vig.stretch_mode = TextureRect.STRETCH_SCALE
	vig.mouse_filter = Control.MOUSE_FILTER_IGNORE
	tint_layer.add_child(vig)

	hud = CanvasLayer.new()
	hud.layer = 2
	add_child(hud)
	var root := Control.new()
	root.set_anchors_preset(Control.PRESET_FULL_RECT)
	root.mouse_filter = Control.MOUSE_FILTER_IGNORE
	root.theme = _make_theme()
	hud.add_child(root)
	_root = root

	# 왼쪽 위: 해·달 + 날짜·시각
	var clock := _card(root, Vector2(14, 12))
	_clock_card = clock
	var ch := HBoxContainer.new()
	ch.add_theme_constant_override("separation", _k(10))
	clock.add_child(ch)
	_sky_icon = TextureRect.new()
	_sky_icon.custom_minimum_size = Vector2(_k(40), _k(40))
	_sky_icon.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	_sky_icon.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	ch.add_child(_sky_icon)
	var cv := VBoxContainer.new()
	cv.add_theme_constant_override("separation", -_k(2))
	ch.add_child(cv)
	_date = _lbl(cv, "", 22)
	_state = _lbl(cv, "", 14, C_DIM)

	# 가운데 위: 일하는 속도 ×1~×4 + 1시간 건너뛰기
	var sp := _card(root, Vector2(470, 12))
	_speed_card = sp
	var sh := HBoxContainer.new()
	sh.add_theme_constant_override("separation", _k(6))
	sp.add_child(sh)
	_lbl(sh, "속도", 14, C_DIM)
	for i in range(s.speed_multipliers.size()):
		var idx := i
		var b := _btn(sh, "×%d" % int(s.speed_multipliers[i]), func(): _set_speed(idx))
		b.toggle_mode = true
		b.custom_minimum_size = Vector2(_k(44), 0)
		_speed_buttons.append(b)
	_speed_label = _lbl(sh, "", 13, C_DIM)
	_skip_btn = _btn(sh, "1시간 건너뛰기 (T)", _skip_hour)

	# 오른쪽 위: 대기 행동, 누적 수확
	var info := _card(root, Vector2(1000, 12))
	_info_card = info
	info.custom_minimum_size = Vector2(_k(266), 0)
	var iv := VBoxContainer.new()
	iv.add_theme_constant_override("separation", 0)
	info.add_child(iv)
	_harvest = _lbl(iv, "", 17)
	_pending = _lbl(iv, "", 14, C_DIM)

	# 아래: 든 도구 알약 + 도구 줄 (그림)
	_tool_pill = _card(root, Vector2(14, 568))
	var pill_style := _style(Color(0.18, 0.14, 0.11, 0.82), 18)
	_tool_pill.add_theme_stylebox_override("panel", pill_style)
	_tool_label = _lbl(_tool_pill, "", 17)
	# 대상 칸 정보 (꽃·꽃봉오리, 열매 개수: 눈으로 셀 수 있는 것만, 명세 2.3)
	_cell_chip = _card(root, Vector2(14, 520))
	_cell_chip.add_theme_stylebox_override("panel", _style(Color(0.18, 0.14, 0.11, 0.82), 18))
	_cell_label = _lbl(_cell_chip, "", 15)
	_cell_chip.visible = false
	var bar_card := _card(root, Vector2(14, 612))
	_bar_card = bar_card
	var bar_box := HBoxContainer.new()
	bar_box.add_theme_constant_override("separation", _k(6))
	bar_card.add_child(bar_box)
	for i in range(Tools.TOOLS.size()):
		var slot := PanelContainer.new()
		slot.custom_minimum_size = Vector2(_k(102), _k(82))
		slot.add_theme_stylebox_override("panel", _style(Color(1, 0.96, 0.88, 0.08), 12, Color(1, 1, 1, 0.08), 4))
		var vb := VBoxContainer.new()
		vb.add_theme_constant_override("separation", 0)
		vb.alignment = BoxContainer.ALIGNMENT_CENTER
		slot.add_child(vb)
		var icon := TextureRect.new()
		icon.texture = Lib.tool_icon(Tools.TOOLS[i]["id"], _k(46))   # 그려질 크기 그대로 다시 그려요 (선명하게)
		icon.custom_minimum_size = Vector2(0, _k(46))
		icon.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
		icon.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
		vb.add_child(icon)
		var nm := _lbl(vb, "%s %s" % [TOOL_KEY_NAMES[i], Tools.TOOLS[i]["label"]], 12)
		nm.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		nm.clip_text = true
		bar_box.add_child(slot)
		_slots.append(slot)
		var idx := i
		slot.mouse_filter = Control.MOUSE_FILTER_STOP
		slot.gui_input.connect(func(ev: InputEvent):
			if ev is InputEventMouseButton and ev.pressed and ev.button_index == MOUSE_BUTTON_LEFT and bar.clickable() and s.can_act():
				s.select_tool(idx)
				bar.peek())
	_slot_on = _style(Color(1.0, 0.86, 0.5, 0.32), 12, C_ACCENT, 4)
	_slot_on.set_border_width_all(maxi(1, _k(2)))
	_slot_off = _style(Color(1, 0.96, 0.88, 0.08), 12, Color(1, 1, 1, 0.08), 4)

	# 알림 (도구 알약 위)
	_note = Label.new()
	_note.position = Vector2(18, 440)
	_note.size = Vector2(_k(900), _k(120))
	_note.vertical_alignment = VERTICAL_ALIGNMENT_BOTTOM
	_note.add_theme_font_size_override("font_size", _k(16))
	_note.add_theme_color_override("font_color", Color(1, 0.9, 0.62))
	_note.add_theme_constant_override("outline_size", _k(6))
	root.add_child(_note)
	var hint := _lbl(root, HelpData.key_hint(), 12, C_DIM)   # 도움말 조작법과 같은 표 (config/help_ko.json)
	_hint = hint

	# 작은 창 (장치·도구 설정·출입문·메뉴·종료)
	_popup = PanelContainer.new()
	_popup.position = Vector2(420, 150)
	_popup.custom_minimum_size = Vector2(_k(440), 0)
	_popup.add_theme_stylebox_override("panel", _style(Color(0.2, 0.15, 0.12, 0.93), 18, Color(1, 0.9, 0.7, 0.3), 18))
	_popup.visible = false
	root.add_child(_popup)
	_popup_box = VBoxContainer.new()
	_popup_box.add_theme_constant_override("separation", _k(10))
	_popup.add_child(_popup_box)
	# 개발자 패널 (진짜 값, F3). M6a 패널 그대로이고 베드 그리드가 없어서 오버레이는 없어요. 처음에는 숨김
	dev = DevPanel.new()
	dev.position = Vector2(_k(14), _k(90))
	dev.size = Vector2(_k(900), _k(420))
	dev.raw_notes = s.raw_rejections
	dev.visible = false
	root.add_child(dev)
	dev.setup(s.runner, null)


# 창 크기에 맞춰 자리 잡기 (매 프레임, 싸요): 위 왼쪽·가운데·오른쪽, 아래 가운데 도구 줄, 그 위 도구 알약·알림, 맨 아래 키 안내
func _layout() -> void:
	var vs := get_viewport().get_visible_rect().size
	var m := float(_k(14))
	for c in [_clock_card, _speed_card, _info_card, _bar_card, _tool_pill, _popup]:
		c.reset_size()
	_clock_card.position = Vector2(m, m)
	_speed_card.position = Vector2(round((vs.x - _speed_card.size.x) * 0.5), m)
	_info_card.position = Vector2(vs.x - _info_card.size.x - m, m)
	var hint_h := float(_k(20))
	var e: float = bar.eased()
	var bar_x := roundf((vs.x - _bar_card.size.x) * 0.5)
	var bar_shown_y := vs.y - _bar_card.size.y - hint_h - m * 0.5
	var bar_hidden_y := vs.y + _k(4)                       # 화면 아래로 완전히 내려가요
	_bar_card.position = Vector2(bar_x, round(lerpf(bar_hidden_y, bar_shown_y, e)))
	_bar_card.visible = e > 0.001
	# 도구 알약: 숨김이면 도구 줄 자리(맨 아래, 키 안내 위)에, 보이면 도구 줄 위에
	var pill_low := vs.y - _tool_pill.size.y - hint_h - m * 0.5
	var pill_high := bar_shown_y - _tool_pill.size.y - _k(6)
	_tool_pill.position = Vector2(bar_x, round(lerpf(pill_low, pill_high, e)))
	_cell_chip.reset_size()
	_cell_chip.position = Vector2(_tool_pill.position.x + _tool_pill.size.x + _k(8), _tool_pill.position.y + _tool_pill.size.y - _cell_chip.size.y)
	_note.size = Vector2(minf(vs.x - 2.0 * m, _k(900)), _k(120))
	_note.position = Vector2(_bar_card.position.x + _k(4), _tool_pill.position.y - _note.size.y - _k(4))
	_hint.position = Vector2(bar_x, vs.y - hint_h)
	_popup.position = ((vs - _popup.size) * 0.5).round()


func _notify(text: String) -> void:
	_notes.append({"text": text, "left": float(Cfg.get_v("visual", "notification_seconds"))})
	if _notes.size() > 4:
		_notes.pop_front()


func _date_text(t: Dictionary) -> String:
	# 분은 일하는 속도 안의 진행을 보여 주는 꾸밈 (틱 안에서 흐르는 현실 시간)
	var minute := int(s.hour_fraction() * 60.0)
	return "%d월 %d일  %02d:%02d" % [t["month"], t["day"], t["hour"], minute]


# 시각(시 + 시간 안의 몫) → 빛 색 (꾸밈, 진짜 값 없음)
const LIGHT_KEYS := [
	[0.0, Color(0.42, 0.47, 0.72)], [4.5, Color(0.45, 0.49, 0.74)], [6.0, Color(0.98, 0.8, 0.74)],
	[7.5, Color(1, 0.97, 0.92)], [12.0, Color(1, 1, 1)], [16.0, Color(1, 0.97, 0.9)],
	[18.0, Color(1.0, 0.88, 0.76)], [19.5, Color(0.74, 0.68, 0.86)], [21.0, Color(0.44, 0.48, 0.73)], [24.0, Color(0.42, 0.47, 0.72)],
]


static func light_at(h: float) -> Color:
	for i in range(LIGHT_KEYS.size() - 1):
		var a: Array = LIGHT_KEYS[i]
		var b: Array = LIGHT_KEYS[i + 1]
		if h >= a[0] and h <= b[0]:
			return (a[1] as Color).lerp(b[1], smoothstep(a[0], b[0], h))
	return LIGHT_KEYS[0][1]


func _refresh(force: bool) -> void:
	var w: Object = s.world()
	var t: Dictionary = w.get_time()
	if force or int(t["tick"]) != _last_tick or s.has_held_plant() != view.preview_plant:   # 정식 미리 보기가 켜지거나 꺼질 때도
		_last_tick = int(t["tick"])
		view.refresh_visuals()
		var tot: Dictionary = w.get_harvest_totals()
		_harvest.text = "누적 수확  %d개 · %.1f kg" % [tot["harvested_fruits"], float(tot["harvested_fresh_g"]) / 1000.0]
	var hf: float = float(t["hour"]) + s.hour_fraction()
	var lc := light_at(hf)
	_tint.color = lc
	view.set_sky(Color("#d6e8ee") * lc)
	view.set_light(hf)
	_sky_icon.texture = Soft.sky_icon(hf >= 6.0 and hf < 19.0, _k(40))
	_date.text = _date_text(t)
	match s.mode:
		CharSession.MODE_SLEEP:
			_state.text = "자는 중… %d시간 남음" % s.remaining_ff_ticks()
		CharSession.MODE_SKIP:
			_state.text = "맡기는 중… %.1f일 남음 (아무 키: 다음 기상 때 멈춤)" % (s.remaining_ff_ticks() / 24.0)
		_:
			_state.text = ("일시정지 · 경과 %d일" % t["elapsed_days"]) if s.paused else ("일하는 중 · 경과 %d일" % t["elapsed_days"])
	_pending.text = "대기 중인 행동 %d개 (다음 시간에 적용, Z 취소)" % (s.held_count() + w.get_pending_command_count())
	_tool_label.text = Tools.describe(s.tool_index, s.tool_settings[s.tool_index], Labels.name_of)
	update_cell_chip()
	for i in range(_slots.size()):
		_slots[i].add_theme_stylebox_override("panel", _slot_on if i == s.tool_index else _slot_off)
	for i in range(_speed_buttons.size()):
		_speed_buttons[i].set_pressed_no_signal(i == s.speed_index)
	_speed_label.text = "1시간 = %d초" % int(round(s.hour_seconds()))
	_skip_btn.disabled = not s.can_skip_hour()
	if dev.visible:
		var tc: int = s.target_cell()
		if tc >= 0:
			dev.cell_index = tc


# Z: 마지막 행동 취소 (Shift+Z: 마지막 묶음 전체). 시간이 시작된 행동은 이미 적용돼서 취소할 수 없어요
func _cancel(whole_stroke: bool) -> void:
	var removed: Array = s.cancel_last_stroke() if whole_stroke else ([s.cancel_last()] if s.held_count() > 0 else [])
	if removed.is_empty():
		_notify("취소할 행동이 없어요. 이미 시작된 시간의 행동은 되돌릴 수 없어요.")
		return
	var name := ActionNames.of(str(removed[-1]["cmd"].get("action", "")))
	if removed.size() == 1:
		_notify("방금 한 %s 취소했어요." % ActionNames.with_object(name))
	else:
		_notify("방금 한 %s %d칸을 취소했어요." % [name, removed.size()])


# 대상 칸이나 틱이 바뀔 때만 다시 써요. 그림이 틱마다 받아 둔 보이는 것(view._last_v)을 같이 써서 스냅샷을 더 복사하지 않아요
func update_cell_chip() -> void:
	var tc: int = s.target_cell() if s.can_act() else -1
	var key := "%d:%d:%s" % [tc, _last_tick, view.preview_plant]
	if key == _cell_key:
		return
	_cell_key = key
	var t := CellInfo.text(view._last_v, tc, s.plant_density(), view.preview_plant)
	_cell_label.text = t
	_cell_chip.visible = t != ""


func _set_speed(i: int) -> void:
	s.set_speed(i)


func _skip_hour() -> void:
	if s.skip_hour():
		_notify("1시간 건너뛰었어요 (대기 중인 행동이 적용됐어요).")


# ---------- 입력 ----------
func _move_input() -> Vector2i:
	var x := 0
	var z := 0
	if Input.is_physical_key_pressed(KEY_A) or Input.is_physical_key_pressed(KEY_LEFT):
		x -= 1
	if Input.is_physical_key_pressed(KEY_D) or Input.is_physical_key_pressed(KEY_RIGHT):
		x += 1
	# 화면 위 = 출입구 쪽 (z 감소). 카메라가 안쪽 끝에서 출입구를 바라봐요
	if Input.is_physical_key_pressed(KEY_W) or Input.is_physical_key_pressed(KEY_UP):
		z -= 1
	if Input.is_physical_key_pressed(KEY_S) or Input.is_physical_key_pressed(KEY_DOWN):
		z += 1
	return Vector2i(x, z)


func _action_held() -> bool:
	return Input.is_physical_key_pressed(KEY_SPACE) or Input.is_physical_key_pressed(KEY_E)


func _input(ev: InputEvent) -> void:
	if ev is InputEventKey and ev.pressed and not ev.echo and (ev.physical_keycode == KEY_TAB or ev.keycode == KEY_TAB):
		if _popup != null and _popup.visible:
			return
		get_viewport().set_input_as_handled()
		if view == null:
			return
		toggle_toolbar()


func toggle_toolbar() -> void:
	bar.toggle()
	s.toolbar_shown = bar.pinned


func _unhandled_input(ev: InputEvent) -> void:
	if view == null or not (ev is InputEventKey) or not ev.pressed or ev.echo:
		return
	var k: Key = ev.physical_keycode if ev.physical_keycode != KEY_NONE else ev.keycode
	if _popup_kind == "help" and _help_scroll_key(k):
		get_viewport().set_input_as_handled()
		return
	if k == KEY_ESCAPE:
		if _popup_kind == "help":
			_open_pause()   # 도움말 → 일시정지 메뉴로 (계속 멈춘 채)
		elif _popup.visible and _popup_kind != "pause":
			_close_popup()
		elif _popup_kind == "pause":
			_close_popup()
		else:
			_open_pause()
		return
	if k == KEY_F3:
		dev.visible = not dev.visible
		return
	if s.mode == CharSession.MODE_SKIP and not s.paused:
		s.interrupt_skip()
		return
	if k == KEY_F5:
		_quick_save()
		return
	if k == KEY_F9:
		_quick_load()
		return
	# 속도·1시간 건너뛰기는 일하는 동안이면 창이 열려 있어도 돼요 (일시정지 중 X)
	if s.mode == CharSession.MODE_WORK and not s.paused:
		if k == KEY_BRACKETLEFT:
			_set_speed(s.speed_index - 1)
			return
		if k == KEY_BRACKETRIGHT:
			_set_speed(s.speed_index + 1)
			return
		if k == KEY_T:
			_skip_hour()
			return
	if k == KEY_Z and s.mode == CharSession.MODE_WORK:
		_cancel(ev.shift_pressed)
		return
	if not s.can_act():
		return
	var ti := TOOL_KEYS.find(k)
	if ti >= 0:
		s.select_tool(ti)
		bar.peek()
	elif k == KEY_R:
		_open_tool_settings()


func _process(delta: float) -> void:
	if view == null:
		return
	s.ui_blocked = _popup.visible
	s.character_frame(_move_input(), _action_held(), delta, Input.is_physical_key_pressed(KEY_SHIFT))
	s.advance_clock(delta)
	for n in _notes:
		n["left"] -= delta
	_notes = _notes.filter(func(n): return n["left"] > 0.0)
	_note.text = "\n".join(_notes.map(func(n): return n["text"]))
	bar.update(delta)
	_layout()
	view.update_dynamic(delta)
	_refresh(false)


# ---------- 작은 창 ----------
func _clear_popup(title: String, kind: String) -> void:
	for c in _popup_box.get_children():
		_popup_box.remove_child(c)
		c.queue_free()
	_popup_kind = kind
	var t := _lbl(_popup_box, title, 20)
	t.add_theme_color_override("font_color", Color(1, 0.85, 0.4))
	_popup.visible = true
	_popup.custom_minimum_size = Vector2(_k(440), 0)
	_popup.size = Vector2(_k(440), 0)


func _close_popup() -> void:
	if _popup_kind == "pause" or _popup_kind == "end":
		s.paused = false
	_popup.visible = false
	_popup_kind = ""


func _row(label: String, c: Control) -> Control:
	var h := HBoxContainer.new()
	var l := Label.new()
	l.text = label
	l.custom_minimum_size = Vector2(_k(150), 0)
	h.add_child(l)
	c.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	h.add_child(c)
	_popup_box.add_child(h)
	return c


func _spin(mn: float, mx: float, st: float, v: float) -> SpinBox:
	var sb := SpinBox.new()
	sb.min_value = mn
	sb.max_value = mx
	sb.step = st
	sb.value = v
	return sb


# 설정 파일 [inputs] 입력칸 (기본값·범위·간격) 그대로의 숫자칸
func _input_spin(input_key: String, v: float) -> SpinBox:
	var r := Cfg.input(input_key)
	return _spin(r["min"], r["max"], r["step"], v)


func _opt(ids: Array, sel: String, raw := false) -> OptionButton:
	var o := OptionButton.new()
	o.focus_mode = Control.FOCUS_NONE
	for id in ids:
		o.add_item(str(id) if raw else Labels.name_of(str(id)))
		o.set_item_metadata(o.item_count - 1, str(id))
		if str(id) == sel:
			o.select(o.item_count - 1)
	return o


func _check(on: bool, text: String) -> CheckBox:
	var c := CheckBox.new()
	c.text = text
	c.button_pressed = on
	c.focus_mode = Control.FOCUS_NONE
	return c


func _buttons(apply_text: String, apply: Callable) -> void:
	var h := HBoxContainer.new()
	_popup_box.add_child(h)
	if apply_text != "":
		_btn(h, apply_text, func():
			apply.call()
			_close_popup())
	_btn(h, "닫기 (Esc)", _close_popup)


func _submit(cmd: Dictionary) -> void:
	s.device_command(cmd)


func _open_device(d: Dictionary) -> void:
	var hc: Dictionary = s.world().get_house_controls()
	var cat: Dictionary = s.catalog
	match d["kind"]:
		"window_panel":
			_open_window_panel(d, hc, cat)
		"heater":
			_clear_popup(d["label"], "device")
			var on := _row("", _check(bool(hc["heater_on"]), "켜기")) as CheckBox
			var hs := Cfg.input("heater_setpoint_degC")
			var sp := _row("설정 온도 (°C)", _spin(float(cat["heater_setpoint_min_degC"]), float(cat["heater_setpoint_max_degC"]), hs["step"],
					float(hc["heater_setpoint_degC"]) if hc["heater_on"] else hs["def"])) as SpinBox
			_buttons("적용", func(): _submit({"action": "set_heater", "on": on.button_pressed, "setpoint_degC": float(sp.value)}))
		"fan":
			_clear_popup(d["label"], "device")
			var on2 := _row("", _check(bool(hc["fan_on"]), "켜기")) as CheckBox
			_buttons("적용", func(): _submit({"action": "set_fan", "on": on2.button_pressed}))
		"drip":
			_clear_popup(d["label"], "device")
			_lbl(_popup_box, "점적 호스: %s" % ("깔려 있어요" if hc["drip_installed"] else "없어요"))
			_buttons("점적 호스 깔기", func(): _submit({"action": "install_drip"}))
		"bench_plant":
			_clear_popup(d["label"], "device")
			_lbl(_popup_box, "하우스 전체를 한 번에 심어요.", 13)
			var g := _row("모종 등급", _opt(Array(cat["seedling_grade"]), "B")) as OptionButton
			var pp := _row("포기/m²", _input_spin("plants_per_m2", Cfg.input("plants_per_m2")["def"])) as SpinBox
			_buttons("하우스 전체에 심기", func(): _submit({"action": "plant",
					"seedling_grade": str(g.get_item_metadata(g.selected)), "plants_per_m2": float(pp.value)}))
		"bench_cover":
			_clear_popup(d["label"], "device")
			var w: Array = Array(cat["cover_change_window_doy"])
			_lbl(_popup_box, "피복 지금 %d겹. 바꿀 수 있는 때: %d~%d일째 (그 밖에는 거부돼요)." % [hc["cover_layers"], w[0], w[1]], 13)
			var ly := _row("겹수", _spin(float(cat["min_cover_layers"]), float(cat["max_cover_layers"]), 1, float(hc["cover_layers"]))) as SpinBox
			_buttons("적용", func(): _submit({"action": "set_cover_layers", "layers": int(ly.value)}))
		"auto_irrigation":
			_clear_popup(d["label"], "device")
			var a: Dictionary = s.runner.auto_irrigation
			_lbl(_popup_box, "정한 시각에 밭 전체에 물을 줘요 (게임 쪽 설정, 명령 기록에 남아요).", 13)
			var on3 := _row("", _check(bool(a["enabled"]), "켜기")) as CheckBox
			var hr := _row("시각", _input_spin("auto_hour", float(a["hour"]))) as SpinBox
			var mm := _row("양 (mm)", _input_spin("auto_mm", float(a["amount_mm"]))) as SpinBox
			_buttons("적용", func():
				s.set_auto_irrigation(on3.button_pressed, int(hr.value), float(mm.value))
				_notify("자동 물 주기: %s" % (("%d시 %.1f mm" % [int(hr.value), mm.value]) if on3.button_pressed else "꺼짐")))
		"door":
			_open_door()


# 측창 조작판: 지금 있는 겹마다 좌·우 개방 단계를 한 창에서 (바뀐 것만 set_window로 넣어요)
func _open_window_panel(d: Dictionary, hc: Dictionary, cat: Dictionary) -> void:
	_clear_popup(d["label"], "device")
	var n := int(hc["cover_layers"])
	var arr: PackedFloat64Array = hc["window_open_fraction"]
	_lbl(_popup_box, "피복 %d겹 · 겹 0 = 가장 바깥 · 좌 = 0행 쪽 벽" % n, 13, C_DIM)
	var ids := []
	for v in cat["window_open_levels"]:
		ids.append("%.2f" % v)
	var grid := GridContainer.new()
	grid.columns = 3
	grid.add_theme_constant_override("h_separation", 12)
	grid.add_theme_constant_override("v_separation", 6)
	_popup_box.add_child(grid)
	for t in ["", "좌", "우"]:
		_lbl(grid, t, 14, C_DIM)
	var opts := []
	for k in range(n):
		_lbl(grid, "%d겹" % k, 15)
		for side in [0, 1]:
			var cur := arr[k * 2 + side] if k * 2 + side < arr.size() else 0.0
			var o := _opt(ids, "%.2f" % cur, true)
			o.custom_minimum_size = Vector2(_k(110), 0)
			grid.add_child(o)
			opts.append([k, "left" if side == 0 else "right", o, cur])
	_buttons("적용", func():
		var sent := 0
		for e in opts:
			var o2: OptionButton = e[2]
			var v := float(str(o2.get_item_metadata(o2.selected)))
			if not is_equal_approx(v, e[3]):
				s.device_command({"action": "set_window", "layer": e[0], "side": e[1], "open_fraction": v})
				sent += 1
		if sent == 0:
			_notify("바뀐 측창이 없어요."))


func _open_door() -> void:
	_clear_popup("출입문", "door")
	_lbl(_popup_box, "장치 설정과 자동 물 주기는 그대로 돌아가요.", 13)
	var hb := HBoxContainer.new()
	_popup_box.add_child(hb)
	_btn(hb, "자러 가기 (다음 %d시까지)" % s.wake_hour, func():
		_close_popup()
		s.go_to_sleep())
	var days := _spin(1, int(Cfg.get_v("char_time", "skip_days_max")), 1, int(Cfg.get_v("char_time", "skip_days_default")))
	_row("며칠 맡기기 (일)", days)
	var hb2 := HBoxContainer.new()
	_popup_box.add_child(hb2)
	_btn(hb2, "맡기기 시작", func():
		_close_popup()
		s.skip_days(int(days.value)))
	_btn(hb2, "닫기 (Esc)", _close_popup)


func _open_tool_settings() -> void:
	var t: Dictionary = Tools.TOOLS[s.tool_index]
	var st: Dictionary = s.tool_settings[s.tool_index]
	_clear_popup("도구 설정: %s" % t["label"], "tool")
	var o: OptionButton = null
	var sp: SpinBox = null
	if t.has("kind_key"):
		o = _row("종류", _opt(Array(s.catalog[t["cat"]]), str(st["kind"]))) as OptionButton
	if t.has("amount_key"):
		sp = _row("양 (%s)" % t["unit"], _input_spin(t["def"], float(st["amount"]))) as SpinBox
	if o == null and sp == null:
		_lbl(_popup_box, "설정할 것이 없어요.")
	_buttons("적용", func():
		if o != null:
			st["kind"] = str(o.get_item_metadata(o.selected))
		if sp != null:
			st["amount"] = float(sp.value))


# 도움말 (일시정지 메뉴에서, 명세 2.4): 왼쪽 주제 목록, 오른쪽 글. Esc나 닫기 → 일시정지 메뉴. 게임은 계속 멈춰 있어요
var help_index := 0
var help_body: RichTextLabel = null
var help_scroll: ScrollContainer = null


func open_help(index: int) -> void:
	var pages := HelpData.pages()
	help_index = clampi(index, 0, pages.size() - 1)
	_clear_popup("도움말", "help")
	s.paused = true
	_popup.custom_minimum_size = Vector2(_k(860), _k(500))
	var hb := HBoxContainer.new()
	hb.add_theme_constant_override("separation", _k(14))
	hb.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_popup_box.add_child(hb)
	var list := VBoxContainer.new()
	list.add_theme_constant_override("separation", _k(4))
	list.custom_minimum_size = Vector2(_k(250), 0)
	hb.add_child(list)
	for i in range(pages.size()):
		var idx := i
		var b := _btn(list, pages[i]["title"], func(): open_help(idx))
		b.toggle_mode = true
		b.set_pressed_no_signal(i == help_index)
		b.alignment = HORIZONTAL_ALIGNMENT_LEFT
		b.add_theme_font_size_override("font_size", _k(14))
	var right := VBoxContainer.new()
	right.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	right.add_theme_constant_override("separation", _k(10))
	hb.add_child(right)
	var head := _lbl(right, pages[help_index]["title"], 19)
	head.add_theme_color_override("font_color", C_ACCENT)
	# 글 자리는 늘 같은 크기(game_config [ui] help_text_height). 긴 글은 이 안에서 스크롤해요(마우스 휠, 얇은 스크롤 막대,
	# ↑↓ PgUp PgDn). 주제를 바꾸면 맨 위부터
	var scroll := ScrollContainer.new()
	scroll.custom_minimum_size = Vector2(_k(560), _k(float(Cfg.get_v("ui", "help_text_height"))))
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	scroll.vertical_scroll_mode = ScrollContainer.SCROLL_MODE_AUTO
	scroll.follow_focus = false
	right.add_child(scroll)
	# 항목 사이는 문단 간격으로 띄우고(빈 줄보다 좁게), 줄 간격도 조금 더
	var body := RichTextLabel.new()
	body.bbcode_enabled = false
	body.fit_content = true
	body.scroll_active = false
	body.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	body.custom_minimum_size = Vector2(_k(540), 0)
	body.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	body.add_theme_font_size_override("normal_font_size", _k(16))
	body.add_theme_color_override("default_color", C_TEXT)
	body.add_theme_constant_override("line_separation", _k(5))
	body.add_theme_constant_override("paragraph_separation", _k(12))
	body.text = pages[help_index]["body"]
	scroll.add_child(body)
	help_body = body
	help_scroll = scroll
	_btn(_popup_box, "닫기 (Esc)", _open_pause)


# 도움말 글 스크롤 (키보드). 한 줄 = 글자 크기 정도, 한 쪽 = 글 자리 높이의 90 %
func _help_scroll_key(k: Key) -> bool:
	if help_scroll == null or not is_instance_valid(help_scroll):
		return false
	var line := float(_k(28))
	var page := help_scroll.size.y * 0.9
	match k:
		KEY_UP:
			help_scroll.scroll_vertical -= int(line)
		KEY_DOWN:
			help_scroll.scroll_vertical += int(line)
		KEY_PAGEUP:
			help_scroll.scroll_vertical -= int(page)
		KEY_PAGEDOWN:
			help_scroll.scroll_vertical += int(page)
		_:
			return false
	return true


# 검사용: 글이 글 자리보다 길어서 스크롤되는지
func help_scrollable() -> bool:
	if help_scroll == null:
		return false
	var sb := help_scroll.get_v_scroll_bar()
	return sb.max_value - sb.page > 1.0


func _open_pause() -> void:
	_clear_popup("일시정지", "pause")
	s.paused = true
	_btn(_popup_box, "계속 (Esc)", _close_popup)
	_btn(_popup_box, "도움말", func(): open_help(0))
	_btn(_popup_box, "빠른 저장 (F5)", func(): _quick_save())
	_btn(_popup_box, "빠른 불러오기 (F9)", func():
		_close_popup()
		_quick_load())
	_btn(_popup_box, "시작 화면으로", func(): get_tree().change_scene_to_file(START_SCENE))


# 작기 끝 (명세 7.3): CharSession이 멈췄어요. "계속 진행"을 누르면 다시 흘러요. 최종 해시는 개발자 패널 기록에만
func _on_season_end(sm: Dictionary) -> void:
	if _season_done:
		return
	_season_done = true
	var h: Dictionary = sm["harvest"]
	_clear_popup("작기가 끝났어요", "end")
	_lbl(_popup_box, "%d일 동안의 작기\n수확 %d개, %.1f kg\n자동 저장 칸에 저장했어요." % [int(sm["tick"]) / 24, h["harvested_fruits"], float(h["harvested_fresh_g"]) / 1000.0])
	s.raw_rejections.append("작기 끝 tick %d 최종 해시 %s" % [sm["tick"], sm["state_hash"]])
	_btn(_popup_box, "계속 진행", _close_popup)
	_btn(_popup_box, "시작 화면으로", func(): get_tree().change_scene_to_file(START_SCENE))


func _quick_save() -> void:
	if not s.can_save():
		_notify("자는 동안에는 저장할 수 없어요.")
		return
	var r: Dictionary = s.save_slot(SaveStore.QUICK)
	if not r["ok"]:
		s.raw_rejections.append("저장 실패: %s" % r.get("message", ""))
	_notify("저장했어요." if r["ok"] else "저장하지 못했어요.")


# 불러오기 실패 알림 (한국어 문장만. 자세한 이유는 개발자 패널 기록에)
static func _load_fail_text(r: Dictionary) -> String:
	match str(r.get("status", "")):
		"missing":
			return "저장해 둔 판이 없어요."
		"other_version":
			return "다른 버전의 게임에서 만든 세이브라서 불러올 수 없어요. 파일은 그대로 두었어요."
	return "세이브 파일을 읽을 수 없어요. 파일은 그대로 두었어요."


func _quick_load() -> void:
	if s.mode != CharSession.MODE_WORK:
		_notify("자는 동안에는 불러올 수 없어요.")
		return
	var r: Dictionary = s.load_slot(SaveStore.QUICK)
	if r["ok"]:
		dev.runner = s.runner
		dev.rebind()
		_season_done = false
		_last_tick = -1
		bar.set_pinned(s.toolbar_shown)
		if _popup.visible:
			_close_popup()
		_notify("불러왔어요.")
	else:
		s.raw_rejections.append("불러오기 실패: %s" % r.get("message", ""))   # 원문은 개발자 패널에만
		_notify(_load_fail_text(r))
