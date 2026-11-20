# 베드 그리드 그리기와 영역 선택. 보이는 것(get_tile_visuals)만 써요 — 진짜 값 숫자는 없어요(결정 D1).
extends Control

const Cfg = preload("res://scripts/cfg.gd")

signal selection_changed(area: Array)   # [row, col, rows, cols], 선택 없으면 빈 배열
signal cell_picked(index: int)          # 누른 칸 (끌기를 시작한 칸). 개발자 패널의 "칸 정보"가 따라가요

var cols := 20
var rows := 4
var cell_px := 44
var visuals: Dictionary = {}
var selection: Array = []               # [row, col, rows, cols]
var flashes: Dictionary = {}            # tile_index -> 남은 초
var overlay := PackedFloat64Array()     # 개발자 오버레이 값 (칸 수와 같은 길이, 없으면 빈 배열). 이 파일은 값의 뜻을 몰라요
var overlay_min := 0.0
var overlay_max := 1.0
var picked_cell := -1                   # 개발자 패널이 보여 주는 칸 (테두리만)

var _drag_from := Vector2i(-1, -1)
var _drag_cur := Vector2i(-1, -1)
var _dragging := false
var _stage_thresholds: Array = []
var _c := {}
var _overlay_low := Color.BLUE
var _overlay_high := Color.YELLOW
var _overlay_alpha := 0.6


func _ready() -> void:
	cell_px = int(Cfg.get_v("visual", "cell_px"))
	_stage_thresholds = Cfg.get_v("visual", "leaf_stage_thresholds_g_per_m2")
	for k in ["soil", "plant_leaf", "plant_leaf_dark", "dead_plant", "flower", "fruit_green", "fruit_ripe",
			"mulch_clear_film", "mulch_black_film", "mulch_white_film", "selection", "harvest_flash"]:
		_c[k] = Cfg.color(k)
	mouse_filter = Control.MOUSE_FILTER_STOP


func set_grid(c: int, r: int) -> void:
	cols = c
	rows = r
	custom_minimum_size = Vector2(cols * cell_px, rows * cell_px)
	size = custom_minimum_size
	queue_redraw()


func set_visuals(v: Dictionary) -> void:
	visuals = v
	queue_redraw()


# 개발자 패널이 칠할 값을 넘겨요 (값 읽기는 개발자 패널 몫). 빈 배열이면 끔.
func set_overlay(values: PackedFloat64Array, mn: float, mx: float) -> void:
	overlay = values
	overlay_min = mn
	overlay_max = mx
	queue_redraw()


func flash(tile_index: int) -> void:
	flashes[tile_index] = float(Cfg.get_v("visual", "harvest_flash_seconds"))


func _process(delta: float) -> void:
	if flashes.is_empty():
		return
	for k in flashes.keys():
		flashes[k] -= delta
		if flashes[k] <= 0.0:
			flashes.erase(k)
	queue_redraw()


func select_all() -> void:
	_set_selection([0, 0, rows, cols])


func clear_selection() -> void:
	_set_selection([])


func _set_selection(a: Array) -> void:
	selection = a
	selection_changed.emit(a)
	queue_redraw()


func _cell_at(p: Vector2) -> Vector2i:
	return Vector2i(clampi(int(p.x) / cell_px, 0, cols - 1), clampi(int(p.y) / cell_px, 0, rows - 1))


func _gui_input(ev: InputEvent) -> void:
	if ev is InputEventMouseButton:
		var mb := ev as InputEventMouseButton
		if mb.button_index == MOUSE_BUTTON_LEFT:
			if mb.pressed:
				_dragging = true
				_drag_from = _cell_at(mb.position)
				_drag_cur = _drag_from
			elif _dragging:
				_dragging = false
				_drag_cur = _cell_at(mb.position)
				var x0 := mini(_drag_from.x, _drag_cur.x)
				var y0 := mini(_drag_from.y, _drag_cur.y)
				var x1 := maxi(_drag_from.x, _drag_cur.x)
				var y1 := maxi(_drag_from.y, _drag_cur.y)
				_set_selection([y0, x0, y1 - y0 + 1, x1 - x0 + 1])
				picked_cell = _drag_from.y * cols + _drag_from.x
				cell_picked.emit(picked_cell)
		elif mb.button_index == MOUSE_BUTTON_RIGHT and mb.pressed:
			clear_selection()
	elif ev is InputEventMouseMotion:
		var mm := ev as InputEventMouseMotion
		if _dragging:
			_drag_cur = _cell_at(mm.position)
			queue_redraw()


func stage_of(leaf_g: float) -> int:
	var s := 0
	for t in _stage_thresholds:
		if leaf_g >= float(t):
			s += 1
	return s


func _draw() -> void:
	if visuals.is_empty():
		return
	var present: PackedByteArray = visuals["crop_present"]
	var alive: PackedByteArray = visuals["crop_alive"]
	var leaf: PackedFloat64Array = visuals["leaf_g_per_m2"]
	var flo: PackedInt32Array = visuals["open_flower_count"]
	var fru: PackedInt32Array = visuals["fruit_count"]
	var rip: PackedInt32Array = visuals["ripening_fruit_count"]
	var mul: PackedStringArray = visuals["mulch_id"]
	var cp := float(cell_px)
	for i in range(rows * cols):
		var r := i / cols
		var c := i % cols
		var rect := Rect2(c * cp, r * cp, cp, cp)
		draw_rect(rect, _c["soil"])
		# 멀칭: 칸 전체 위에 필름 색 (투명 필름은 옅게)
		var m: String = mul[i]
		if m != "none":
			var mc: Color = _c.get("mulch_" + m, Color(0.6, 0.6, 0.6))
			mc.a = 0.3 if m == "clear_film" else 0.75
			draw_rect(rect.grow(-3.0), mc)
		var ctr := rect.get_center()
		if present[i] == 1:
			if alive[i] == 1:
				var s := stage_of(leaf[i])
				var rad := 5.0 + 4.0 * s
				draw_circle(ctr, rad, _c["plant_leaf_dark"] if s >= 2 else _c["plant_leaf"])
				draw_circle(ctr, rad * 0.6, _c["plant_leaf"])
				if flo[i] > 0:  # 꽃: 흰 점 (있음/없음만)
					for k in range(3):
						draw_circle(ctr + Vector2(-8 + 8 * k, -rad - 3), 2.2, _c["flower"])
				if fru[i] > 0:  # 과실: 연두 점, 익어 가는 것: 빨간 점
					for k in range(3):
						draw_circle(ctr + Vector2(-8 + 8 * k, rad + 4), 2.6, _c["fruit_green"])
				if rip[i] > 0:
					for k in range(2):
						draw_circle(ctr + Vector2(-4 + 8 * k, rad + 4), 3.2, _c["fruit_ripe"])
			else:  # 죽은 포기: 누런 X
				var h := 10.0
				draw_line(ctr + Vector2(-h, -h), ctr + Vector2(h, h), _c["dead_plant"], 3.0)
				draw_line(ctr + Vector2(-h, h), ctr + Vector2(h, -h), _c["dead_plant"], 3.0)
		else:  # 빈 칸: 작은 점선 네모
			draw_rect(Rect2(ctr - Vector2(3, 3), Vector2(6, 6)), Color(0, 0, 0, 0.25))
		draw_rect(rect, Color(0, 0, 0, 0.35), false, 1.0)
		if flashes.has(i):
			var a: float = clampf(flashes[i] / float(Cfg.get_v("visual", "harvest_flash_seconds")), 0.0, 1.0)
			var fc: Color = _c["harvest_flash"]
			fc.a = a * 0.7
			draw_rect(rect.grow(-2.0), fc)
	_draw_overlay(cp)
	# 개발자 패널이 보여 주는 칸 테두리
	if picked_cell >= 0 and picked_cell < rows * cols:
		var pc := Rect2((picked_cell % cols) * cp, (picked_cell / cols) * cp, cp, cp)
		draw_rect(pc.grow(-1.0), Color(1, 1, 1, 0.8), false, 1.5)
	# 선택 테두리 (끌고 있는 동안은 끄는 사각형)
	var sel := selection
	if _dragging:
		var x0 := mini(_drag_from.x, _drag_cur.x)
		var y0 := mini(_drag_from.y, _drag_cur.y)
		sel = [y0, x0, absi(_drag_cur.y - _drag_from.y) + 1, absi(_drag_cur.x - _drag_from.x) + 1]
	if not sel.is_empty():
		var sr := Rect2(sel[1] * cp, sel[0] * cp, sel[3] * cp, sel[2] * cp)
		var fill: Color = _c["selection"]
		fill.a = 0.18
		draw_rect(sr, fill)
		draw_rect(sr, _c["selection"], false, 3.0)


# 개발자 오버레이: 값을 [min, max] 구간의 색으로 칠해요 (값이 없거나 길이가 안 맞으면 안 그려요)
func _draw_overlay(cp: float) -> void:
	if overlay.size() != rows * cols:
		return
	var span := overlay_max - overlay_min
	for i in range(overlay.size()):
		var f := 0.5 if span <= 0.0 else clampf((overlay[i] - overlay_min) / span, 0.0, 1.0)
		var col := _overlay_low.lerp(_overlay_high, f)
		col.a = _overlay_alpha
		draw_rect(Rect2((i % cols) * cp, (i / cols) * cp, cp, cp).grow(-1.0), col)
