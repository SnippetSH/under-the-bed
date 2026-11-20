# 저장·불러오기 목록 (시작 화면과 본 화면이 같이 써요). SaveStore.list_saves()의 칸을 한 줄씩 보여줘요.
# - 저장 단추(allow_save): 수동 칸에만. 빠른·자동 저장 칸은 불러오기만 (자동은 게임이 알아서 써요)
# - 불러오기 단추: 불러올 수 있는 칸만 눌려요. 다른 버전·깨진 세이브는 흐리게 하고 이유를 줄 아래에 적어요 (D6)
extends PanelContainer

const Cfg = preload("res://scripts/cfg.gd")
const SaveStore = preload("res://scripts/save_store.gd")

signal save_requested(id: String)
signal load_requested(id: String)
signal closed

var allow_save := false
var show_close := false

var _rows: VBoxContainer


func setup(allow_save_: bool, show_close_: bool) -> void:
	allow_save = allow_save_
	show_close = show_close_
	var v := VBoxContainer.new()
	add_child(v)
	var head := HBoxContainer.new()
	v.add_child(head)
	var title := Label.new()
	title.text = "저장·불러오기" if allow_save else "저장된 판 불러오기"
	title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	title.add_theme_font_size_override("font_size", 18)
	head.add_child(title)
	if show_close:
		var cb := Button.new()
		cb.text = "닫기"
		cb.focus_mode = Control.FOCUS_NONE
		cb.pressed.connect(func(): closed.emit())
		head.add_child(cb)
	var scroll := ScrollContainer.new()
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	v.add_child(scroll)
	_rows = VBoxContainer.new()
	_rows.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_rows.add_theme_constant_override("separation", 6)
	scroll.add_child(_rows)
	refresh()


func refresh() -> void:
	for c in _rows.get_children():
		_rows.remove_child(c)
		c.queue_free()
	for e in SaveStore.list_saves():
		if e["kind"] == "other" and not e["exists"]:
			continue
		_rows.add_child(_make_row(e))


func _make_row(e: Dictionary) -> Control:
	var box := VBoxContainer.new()
	var row := HBoxContainer.new()
	box.add_child(row)
	var name_label := Label.new()
	name_label.text = e["label"]
	name_label.custom_minimum_size = Vector2(110, 0)
	row.add_child(name_label)
	var info := Label.new()
	info.text = SaveStore.describe(e)
	info.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	info.clip_text = true
	row.add_child(info)
	if allow_save and e["kind"] == "manual":
		var sb := Button.new()
		sb.text = "저장"
		sb.focus_mode = Control.FOCUS_NONE
		var sid: String = e["id"]
		sb.pressed.connect(func(): save_requested.emit(sid))
		row.add_child(sb)
	var lb := Button.new()
	lb.text = "불러오기"
	lb.focus_mode = Control.FOCUS_NONE
	lb.disabled = not e["can_load"]
	var lid: String = e["id"]
	lb.pressed.connect(func(): load_requested.emit(lid))
	row.add_child(lb)
	if e["exists"] and not e["can_load"]:
		info.modulate = Color(1, 1, 1, 0.5)
		name_label.modulate = Color(1, 1, 1, 0.5)
		var n := Label.new()
		n.text = e["notice"]
		n.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		n.custom_minimum_size = Vector2(400, 0)
		n.add_theme_font_size_override("font_size", 12)
		n.add_theme_color_override("font_color", Cfg.color("warning"))
		box.add_child(n)
	return box
