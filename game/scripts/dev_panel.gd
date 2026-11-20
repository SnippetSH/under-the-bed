# 개발자 패널 (진짜 값, 검증용). M6 기본 켜짐, F3로 켜고 꺼요. M7에서 측정으로 바뀌어요.
# get_true_* 는 이 파일(과 game/scripts/dev/)에서만 불러요 (E8 검사 대상, tools/check_true_access.py).
# 구성 (m6-overview 5.3): 오버레이(칸별 값 하나를 색으로), 칸·작물 정보, 하우스, 이벤트 기록, 상태 해시(눌렀을 때만).
# 패널을 끄면(숨기면) 오버레이도 같이 꺼져서 게임 화면에는 진짜 값 흔적이 남지 않아요 (H8).
extends PanelContainer

const Cfg = preload("res://scripts/cfg.gd")

var runner: Node
var bed: Control
var cell_index := 0

var _overlay_opt: OptionButton
var _legend: Label
var _hash_label: Label
var _cell_text: RichTextLabel
var _crop_text: RichTextLabel
var _house_text: RichTextLabel
var _event_text: RichTextLabel
var _options: Array = []   # [{label, kind, field}] (0번은 "오버레이 없음")
var _acc := 0.0
var _interval := 0.25
var _event_lines := 60


func setup(r: Node, b: Control) -> void:
	runner = r
	bed = b
	_interval = float(Cfg.get_v("visual", "dev_refresh_seconds"))
	_event_lines = int(Cfg.get_v("visual", "dev_event_lines"))
	var vb := VBoxContainer.new()
	add_child(vb)
	var head := HBoxContainer.new()
	head.add_theme_constant_override("separation", 10)
	vb.add_child(head)
	var title := Label.new()
	title.text = "개발자 패널 (진짜 값) — M7에서 측정으로 바뀌어요  [F3]"
	title.add_theme_color_override("font_color", Cfg.color("dev_title"))
	head.add_child(title)
	var b_hash := Button.new()
	b_hash.text = "상태 해시 계산"
	b_hash.focus_mode = Control.FOCUS_NONE
	b_hash.pressed.connect(_on_hash)
	head.add_child(b_hash)
	_hash_label = Label.new()
	_hash_label.text = "(누르면 지금 틱의 해시)"
	head.add_child(_hash_label)
	var ov := HBoxContainer.new()
	ov.add_theme_constant_override("separation", 8)
	vb.add_child(ov)
	var ovl := Label.new()
	ovl.text = "오버레이"
	ov.add_child(ovl)
	_overlay_opt = OptionButton.new()
	_overlay_opt.focus_mode = Control.FOCUS_NONE
	_overlay_opt.item_selected.connect(func(_i: int): refresh())
	ov.add_child(_overlay_opt)
	_legend = Label.new()
	_legend.text = ""
	ov.add_child(_legend)
	var tabs := TabContainer.new()
	tabs.size_flags_vertical = Control.SIZE_EXPAND_FILL
	vb.add_child(tabs)
	var cell_tab := HBoxContainer.new()
	cell_tab.name = "칸·작물"
	tabs.add_child(cell_tab)
	_cell_text = _mk(cell_tab)
	_crop_text = _mk(cell_tab)
	_house_text = _mk(tabs)
	_house_text.name = "하우스"
	_event_text = _mk(tabs)
	_event_text.name = "이벤트 기록"
	bed.cell_picked.connect(func(i: int): cell_index = i)
	visibility_changed.connect(_on_visibility_changed)
	rebind()


func _mk(parent: Node) -> RichTextLabel:
	var r := RichTextLabel.new()
	r.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	r.size_flags_vertical = Control.SIZE_EXPAND_FILL
	r.bbcode_enabled = true
	r.scroll_active = true
	r.add_theme_font_size_override("normal_font_size", 11)
	r.add_theme_font_size_override("bold_font_size", 11)
	parent.add_child(r)
	return r


# 월드가 바뀌었을 때 오버레이 목록을 다시 만들어요 (무리·병 id는 칸·작물 값의 키에서 와요).
func rebind() -> void:
	var w: Object = runner.world
	var items: Array = [{"label": "(없음)", "kind": "", "field": ""}]
	for f in ["water_mm", "soil_temp_degC", "ph", "ec_dS_per_m", "nh4_N_g_per_m2", "no2_N_g_per_m2", "no3_N_g_per_m2",
			"aeration_index", "compaction_index"]:
		items.append({"label": "흙 " + f, "kind": "soil", "field": f})
	for g in (w.get_true_cell(0).get("microbe_index", {}) as Dictionary).keys():
		items.append({"label": "흙 미생물 " + str(g), "kind": "soil", "field": "microbe_index." + str(g)})
	for f in ["leaf_g_per_m2", "root_health", "fruit_g_per_m2", "infection_anthracnose_index", "infection_botrytis_index",
			"infection_powdery_mildew_index", "spider_mite_index", "predator_mite_index", "canopy_rh_index"]:
		items.append({"label": "작물 " + f, "kind": "crop", "field": f})
	for d in (w.get_true_crop(0).get("infection_index", {}) as Dictionary).keys():
		items.append({"label": "작물 감염 " + str(d), "kind": "crop", "field": "infection_index." + str(d)})
	for k in (w.get_true_crop(0).get("stress_hours", {}) as Dictionary).keys():
		items.append({"label": "작물 스트레스시간 " + str(k), "kind": "crop", "field": "stress_hours." + str(k)})
	_options = items
	var keep := _overlay_opt.selected
	_overlay_opt.clear()
	for it in items:
		_overlay_opt.add_item(it["label"])
	_overlay_opt.select(clampi(keep, 0, items.size() - 1))


func _on_visibility_changed() -> void:
	if bed == null:
		return
	if visible:
		refresh()
	else:
		bed.set_overlay(PackedFloat64Array(), 0.0, 1.0)


func _on_hash() -> void:
	_hash_label.text = "tick %d  hash %s" % [runner.world.get_tick(), runner.world.state_hash()]


func _process(delta: float) -> void:
	if not visible or runner == null or runner.world == null:
		return
	_acc += delta
	if _acc >= _interval:
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


static func format_event(e: Dictionary, cols: int) -> String:
	var s := "t%d [%s]" % [int(e["tick"]), e["type"]]
	if e.has("tile_index"):
		var i := int(e["tile_index"])
		s += " 칸 %d (행 %d, 열 %d)" % [i, i / cols, i % cols]
	for k in e:
		if k != "tick" and k != "type" and k != "tile_index":
			s += " %s=%s" % [k, fmt(e[k])]
	return s


func refresh() -> void:
	var w: Object = runner.world
	var t: Dictionary = w.get_time()
	var g: Vector2i = w.get_grid_size()
	_update_overlay()
	var head := "tick %d  (%d월 %d일 %02d시, day_of_year %d)\n수확 합계: %s\n대기 명령 %d\n" % [
		t["tick"], t["month"], t["day"], t["hour"], t["day_of_year"], fmt(w.get_harvest_totals()), w.get_pending_command_count()]
	_house_text.text = ""
	_house_text.append_text(head + dump(w.get_true_house(), "하우스 (진짜 값)"))
	var idx := clampi(cell_index, 0, g.x * g.y - 1)
	_cell_text.text = ""
	_cell_text.append_text(dump(w.get_true_cell(idx), "칸 %d (행 %d, 열 %d) 흙" % [idx, idx / g.x, idx % g.x]))
	_crop_text.text = ""
	_crop_text.append_text(dump(w.get_true_crop(idx), "칸 %d 작물" % idx))
	var lines := PackedStringArray()
	var ev: Array = runner.event_log
	for i in range(maxi(0, ev.size() - _event_lines), ev.size()):
		lines.push_back(format_event(ev[i], g.x))
	lines.reverse()
	_event_text.text = ""
	_event_text.append_text("[b]이벤트 기록 (최근 %d개, 위가 최신)[/b]\n" % lines.size() + "\n".join(lines))


func _update_overlay() -> void:
	var sel := _overlay_opt.selected
	if sel <= 0 or sel >= _options.size():
		bed.set_overlay(PackedFloat64Array(), 0.0, 1.0)
		_legend.text = ""
		return
	var o: Dictionary = _options[sel]
	var col: PackedFloat64Array = runner.world.get_true_column(o["kind"], o["field"])
	if col.is_empty():
		bed.set_overlay(PackedFloat64Array(), 0.0, 1.0)
		_legend.text = "(이 값은 읽을 수 없어요)"
		return
	var mn := col[0]
	var mx := col[0]
	for v in col:
		mn = minf(mn, v)
		mx = maxf(mx, v)
	bed.set_overlay(col, mn, mx)
	_legend.text = "파랑 = %s  →  노랑 = %s" % [fmt(mn), fmt(mx)]
