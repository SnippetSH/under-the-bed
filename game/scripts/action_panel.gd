# 행동 패널: 행동 17종(m6-overview 6.2)을 고르고 입력칸을 채워 명령을 넣어요.
# - 선택지(종류 목록, 창 개방 단계, 난방·겹수 범위)는 SimWorld.get_catalog()에서 와요.
# - "지금 할 수 있나"는 can_enqueue(코어와 같은 판정)로 미리 봐요: 지금 거부될 행동은 목록에서 "(지금은 안 돼요)"로 흐리게
#   보여 주고 이유를 적어요. 그래도 실행 단추는 눌려요: 누르면 코어가 거부하고 알림이 떠요 (H3).
# - 이 파일은 진짜 값(get_true_*)을 읽지 않아요.
extends PanelContainer

const Cfg = preload("res://scripts/cfg.gd")
const Labels = preload("res://scripts/labels.gd")

signal submitted(action: Dictionary, result: Dictionary)   # 명령을 넣은 뒤 (받아들여졌든 거부됐든)
signal area_missing(action: Dictionary)                    # 영역 행동인데 영역이 없을 때

var runner: Node
var bed: Control

var _cat: Dictionary = {}
var _actions: Array = []
var _opt: OptionButton
var _form: VBoxContainer
var _reason: Label
var _run: Button
var _fields := {}
var _avail: Array = []       # 행동마다 {ok, message}
var _acc := 0.0
var _force := true
var _layers_seen := -1
var _refresh_seconds := 0.25


func setup(r: Node, b: Control) -> void:
	runner = r
	bed = b
	_refresh_seconds = float(Cfg.get_v("visual", "action_refresh_seconds"))
	var v := VBoxContainer.new()
	add_child(v)
	var title := Label.new()
	title.text = "행동 (영역 행동은 먼저 영역을 고르세요)"
	v.add_child(title)
	_opt = OptionButton.new()
	_opt.focus_mode = Control.FOCUS_NONE
	_opt.item_selected.connect(func(_i: int): _rebuild_form())
	v.add_child(_opt)
	_form = VBoxContainer.new()
	v.add_child(_form)
	_reason = Label.new()
	_reason.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_reason.custom_minimum_size = Vector2(320, 0)
	_reason.add_theme_font_size_override("font_size", 12)
	v.add_child(_reason)
	_run = Button.new()
	_run.text = "실행"
	_run.focus_mode = Control.FOCUS_NONE
	_run.custom_minimum_size = Vector2(0, 36)
	_run.pressed.connect(run_selected)
	v.add_child(_run)
	var hint := Label.new()
	hint.text = "넣은 명령은 다음 1시간이 시작될 때 적용돼요.\n취소할 수 없어요. 일시정지 중에도 넣을 수 있어요."
	hint.add_theme_font_size_override("font_size", 11)
	hint.modulate = Color(1, 1, 1, 0.7)
	v.add_child(hint)
	rebind()


# 월드가 바뀌었을 때(새 판·불러오기) 카탈로그를 다시 읽고 목록을 다시 만들어요.
func rebind() -> void:
	_cat = runner.world.get_catalog()
	_actions = Labels.actions(_cat)
	var keep := _opt.selected
	_opt.clear()
	_avail.clear()
	for a in _actions:
		_opt.add_item(a["label"])
		_avail.append({"ok": true, "message": ""})
	_opt.select(clampi(keep, 0, _actions.size() - 1))
	_layers_seen = -1
	_rebuild_form()
	mark_dirty()


func mark_dirty() -> void:
	_force = true


func selected_action() -> Dictionary:
	return _actions[_opt.selected]


# id로 행동을 골라요 (화면 조작과 같은 경로: 입력칸이 새로 만들어져요)
func select_action(action_id: String) -> bool:
	for i in range(_actions.size()):
		if _actions[i]["id"] == action_id:
			_opt.select(i)
			_rebuild_form()
			return true
	return false


func action_ids() -> Array:
	return _actions.map(func(a: Dictionary): return a["id"])


func action_count() -> int:
	return _actions.size()


func _process(delta: float) -> void:
	if runner == null or runner.world == null or not is_visible_in_tree():
		return
	_acc += delta
	if _force or _acc >= _refresh_seconds:
		_acc = 0.0
		_force = false
		refresh_availability()


# ---------- 입력칸 ----------
func _rebuild_form() -> void:
	for c in _form.get_children():
		_form.remove_child(c)
		c.queue_free()
	_fields.clear()
	var a := selected_action()
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
				sb.min_value = f["min"]
				sb.max_value = f["max"]
				sb.step = f["step"]
				sb.value = f["def"]
				sb.value_changed.connect(func(_v: float): mark_dirty())
				c = sb
			"bool":
				var cb := CheckBox.new()
				cb.button_pressed = f.get("def", true)
				cb.focus_mode = Control.FOCUS_NONE
				cb.toggled.connect(func(_on: bool): mark_dirty())
				c = cb
			"cat":
				c = _opt_from_ids(Array(_cat.get(f["cat"], PackedStringArray())), f.get("def_id", ""), false)
			"side":
				c = _opt_from_ids(["left", "right"], "", false)
			"layer":
				var ids := []
				for i in range(int(runner.world.get_house_controls()["cover_layers"])):
					ids.append(str(i))
				c = _opt_from_ids(ids, "", true)
			"levels":
				var ids2 := []
				for v in _cat.get("window_open_levels", PackedFloat64Array()):
					ids2.append("%.2f" % v)
				c = _opt_from_ids(ids2, "", true)
		c.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		row.add_child(c)
		_fields[f["key"]] = c
	mark_dirty()


func _opt_from_ids(ids: Array, def_id: String, raw: bool) -> OptionButton:
	var o := OptionButton.new()
	o.focus_mode = Control.FOCUS_NONE
	for id in ids:
		o.add_item(str(id) if raw else Labels.name_of(str(id)))
		o.set_item_metadata(o.item_count - 1, str(id))
		if str(id) == def_id:
			o.select(o.item_count - 1)
	o.item_selected.connect(func(_i: int): mark_dirty())
	return o


# 입력칸 값으로 명령 사전을 만들어요 (영역 행동의 area는 bed.selection)
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
				cmd[f["key"]] = str(o.get_item_metadata(o.selected)) if o.selected >= 0 else ""
			"layer":
				var o2 := c as OptionButton
				cmd[f["key"]] = int(str(o2.get_item_metadata(o2.selected))) if o2.selected >= 0 else 0
			"levels":
				var o3 := c as OptionButton
				cmd[f["key"]] = float(str(o3.get_item_metadata(o3.selected))) if o3.selected >= 0 else 0.0
	return cmd


# ---------- 지금 할 수 있나 (can_enqueue) ----------
func refresh_availability() -> void:
	var w: Object = runner.world
	var layers := int(w.get_house_controls()["cover_layers"])
	if layers != _layers_seen:
		var first := _layers_seen < 0
		_layers_seen = layers
		if not first and selected_action()["id"] == "set_window":
			_rebuild_form()
	var area: Array = bed.selection if not bed.selection.is_empty() else [0, 0, bed.rows, bed.cols]
	for i in range(_actions.size()):
		var act: Dictionary = _actions[i]
		var r: Dictionary = w.can_enqueue(Labels.default_command(act, _cat, area))
		var ok: bool = r["ok"]
		_avail[i] = {"ok": ok, "message": str(r["message"])}
		var text: String = act["label"] if ok else "%s  (지금은 안 돼요)" % act["label"]
		if _opt.get_item_text(i) != text:
			_opt.set_item_text(i, text)
	var a := selected_action()
	var msg := ""
	var ok_now := true
	if a["area"] and bed.selection.is_empty():
		msg = "먼저 영역을 고르세요 (끌어서 사각형, 또는 전체 선택)."
		ok_now = _avail[_opt.selected]["ok"]
	else:
		var r2: Dictionary = w.can_enqueue(build_command(a))
		ok_now = r2["ok"]
		if not ok_now:
			msg = "지금은 거부돼요: %s\n(%s)" % [Labels.reject_summary(a["id"]), r2["message"]]
	_reason.text = msg
	_reason.add_theme_color_override("font_color", Cfg.color("warning") if not ok_now else Cfg.color("notice"))
	_run.modulate = Color(1, 1, 1, 1.0 if ok_now else 0.55)


func is_available(action_id: String) -> bool:
	for i in range(_actions.size()):
		if _actions[i]["id"] == action_id:
			return _avail[i]["ok"]
	return false


# 실행 단추와 같은 동작
func run_selected() -> void:
	var a := selected_action()
	if a["area"] and bed.selection.is_empty():
		area_missing.emit(a)
		return
	var r: Dictionary = runner.submit(build_command(a))
	submitted.emit(a, r)
	mark_dirty()
