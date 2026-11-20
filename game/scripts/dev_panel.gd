# 개발자 패널 (진짜 값, 검증용). M6 기본 켜짐, F3로 켜고 꺼요. M7에서 측정으로 바뀌어요.
# get_true_* 는 이 파일에서만 불러요(E8 검사 대상).
extends PanelContainer

var runner: Node
var bed: Control
var _house: RichTextLabel
var _cell: RichTextLabel
var _crop: RichTextLabel
var _hash_label: Label
var _acc := 0.0
var cell_index := 0


func _ready() -> void:
	var vb := VBoxContainer.new()
	add_child(vb)
	var head := HBoxContainer.new()
	vb.add_child(head)
	var title := Label.new()
	title.text = "개발자 패널 (진짜 값) — M7에서 측정으로 바뀌어요  [F3]"
	title.add_theme_color_override("font_color", Color(1, 0.8, 0.3))
	head.add_child(title)
	var b := Button.new()
	b.text = "상태 해시 계산"
	b.focus_mode = Control.FOCUS_NONE
	b.pressed.connect(_on_hash)
	head.add_child(b)
	_hash_label = Label.new()
	_hash_label.text = "(누르면 지금 틱의 해시)"
	head.add_child(_hash_label)
	var cols := HBoxContainer.new()
	cols.size_flags_vertical = Control.SIZE_EXPAND_FILL
	vb.add_child(cols)
	_house = _mk(cols)
	_cell = _mk(cols)
	_crop = _mk(cols)


func _mk(parent: Node) -> RichTextLabel:
	var r := RichTextLabel.new()
	r.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	r.size_flags_vertical = Control.SIZE_EXPAND_FILL
	r.scroll_active = true
	r.add_theme_font_size_override("normal_font_size", 11)
	parent.add_child(r)
	return r


func _on_hash() -> void:
	var t: int = runner.world.get_tick()
	_hash_label.text = "tick %d  hash %s" % [t, runner.world.state_hash()]


func _process(delta: float) -> void:
	if not visible or runner == null or runner.world == null:
		return
	_acc += delta
	if _acc >= 0.25:  # 4 Hz
		_acc = 0.0
		refresh()


static func fmt(v: Variant) -> String:
	match typeof(v):
		TYPE_FLOAT:
			return str(snappedf(v, 0.0001))
		TYPE_PACKED_FLOAT64_ARRAY:
			var parts := PackedStringArray()
			for x in v:
				parts.push_back(str(snappedf(x, 0.001)))
			return "[" + ", ".join(parts) + "]"
		TYPE_DICTIONARY:
			var parts2 := PackedStringArray()
			for k in v:
				parts2.push_back("%s=%s" % [k, fmt(v[k])])
			return "{" + ", ".join(parts2) + "}"
		TYPE_ARRAY:
			return "[%d개]" % v.size() if v.size() > 4 else str(v)
		_:
			return str(v)


static func dump(d: Dictionary, head: String) -> String:
	var s := "[b]%s[/b]\n" % head
	for k in d:
		s += "%s: %s\n" % [k, fmt(d[k])]
	return s


func refresh() -> void:
	var w: Object = runner.world
	var t: Dictionary = w.get_time()
	var hv: Dictionary = w.get_true_house()
	var tot: Dictionary = w.get_harvest_totals()
	var head := "tick %d  (day %d, %02d시)\n수확 합계: %s\n" % [t["tick"], t["day_of_year"], t["hour"], fmt(tot)]
	head += "대기 명령 %d\n" % w.get_pending_command_count()
	_house.text = ""
	_house.append_text(head + dump(hv, "하우스 (true)"))
	var idx := cell_index
	var g: Vector2i = w.get_grid_size()
	_cell.text = ""
	_cell.append_text(dump(w.get_true_cell(idx), "칸 %d (행 %d, 열 %d) 흙" % [idx, idx / g.x, idx % g.x]))
	_crop.text = ""
	_crop.append_text(dump(w.get_true_crop(idx), "칸 %d 작물" % idx))
