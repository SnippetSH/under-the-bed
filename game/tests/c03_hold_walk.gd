# C3 (m6b-character-play 9.1): 행동 키를 누른 채 통로 끝에서 끝까지 걸어요 → 지나간 칸마다 정확히 한 번, 빠진 칸·겹친 칸 없음.
# 덤: 걷는 동안 시간이 흐르지 않음, 키를 떼지 않고 되돌아가도 같은 칸 다시 안 함(명세 5.1), 누른 동안 방향 고정(명세 4장),
# 달리기 = 걷기 × run_speed_factor(R17), 달리며 낮은 프레임(한 프레임에 1칸 넘게)에서도 칸마다 한 번(0.2 m씩 나눠 걷기).
#   godot --headless --path game --script res://tests/c03_hold_walk.gd -- out=<폴더>
extends SceneTree

const U = preload("res://tests/util.gd")
const Driver = preload("res://tests/char_driver.gd")
const SaveStore = preload("res://scripts/save_store.gd")

var fails := PackedStringArray()


func _ok(name: String, cond: bool, detail := "") -> void:
	if not cond:
		fails.push_back("%s %s" % [name, detail])
	print("%s %s %s" % ["ok  " if cond else "FAIL", name, detail])


func _cells(log: Array, cols: int) -> Array:
	return log.map(func(e): return int(e["command"]["area"][0]) * cols + int(e["command"]["area"][1]))


func _init() -> void:
	var args := U.parse_args()
	SaveStore.dir = String(args["out"]).path_join("c03_saves")
	var s: Object = Driver.new_session("direct_play", false)
	if s == null:
		fails.push_back("start failed")
		U.finish(self, fails)
		return
	var d: Object = Driver.new(s)
	var cols: int = s.cols
	var L: Object = s.layout

	# ---- 가운데 통로를 끝에서 끝까지, 오른쪽 베드의 2행 (물뿌리개) ----
	var log1: int = s.world().get_command_log().size()
	var t_before: int = s.world().get_tick()
	d.hold_walk(1, 1, 0, -0.6, L.length_m + 0.6)
	_ok("C3_no_time_passed_while_walking", s.world().get_tick() == t_before)
	_ok("C3_facing_locked_while_held", s.facing == Vector2i(1, 0), str(s.facing))
	d.wait_ticks(1)
	var l3: Array = s.world().get_command_log().slice(log1)
	var cells := _cells(l3, cols)
	var seen := {}
	var dup := 0
	for c in cells:
		if seen.has(c):
			dup += 1
		seen[c] = true
	var missing := 0
	for c in range(cols):
		if not seen.has(2 * cols + c):
			missing += 1
	var others := seen.keys().filter(func(k): return k / cols != 2).size()
	_ok("C3_each_passed_cell_exactly_once", l3.size() == cols and dup == 0 and missing == 0 and others == 0,
			"commands=%d dup=%d missing=%d other_rows=%d" % [l3.size(), dup, missing, others])
	_ok("C3_in_walking_order", cells == range(2 * cols, 3 * cols), str(cells))

	# ---- 왼쪽(1행)을 보며 4.5 m까지 갔다가 키를 떼지 않고 되돌아오기: 0~4열 다섯 칸만 ----
	var log2: int = s.world().get_command_log().size()
	d.walk_to(Vector2(L.aisle_center_x(1), 0.5))
	d.face(Vector2i(-1, 0))
	d._frame(Vector2i.ZERO, true)
	d._walk_axis(1, 4.5, true)
	d._walk_axis(1, 0.5, true)
	d._frame(Vector2i.ZERO, false)
	s.flush_held()
	var l4: Array = s.world().get_command_log().slice(log2)
	_ok("C3_back_and_forth_same_hold_no_repeat", l4.size() == 5 and _cells(l4, cols) == range(cols, cols + 5), "commands=%d %s" % [l4.size(), _cells(l4, cols)])

	# ---- 달리기 (Shift) ----
	d.walk_to(Vector2(L.aisle_center_x(1), 10.5))
	var pz: float = s.pos.y
	s.character_frame(Vector2i(0, 1), false, 0.1, false)
	var walk_d: float = s.pos.y - pz
	pz = s.pos.y
	s.character_frame(Vector2i(0, 1), false, 0.1, true)
	var run_d: float = s.pos.y - pz
	_ok("run_speed_is_walk_times_run_factor", is_equal_approx(run_d / walk_d, s.run_factor) and s.running and is_equal_approx(s.run_factor, 1.75),
			"walk=%.3f run=%.3f factor=%.2f" % [walk_d, run_d, s.run_factor])
	for case in [[2, -1, 1.0 / 60.0, "row3_run_60fps"], [0, 1, 0.5, "row0_run_2fps"]]:
		d.walk_to(Vector2(L.aisle_center_x(case[0]), -0.6))
		d.face(Vector2i(case[1], 0))
		s.select_tool(0)
		var lg: int = s.world().get_command_log().size()
		d.run = true
		d.frame_dt = case[2]
		d._frame(Vector2i.ZERO, true)
		d._walk_axis(1, L.length_m + 0.6, true)
		d._frame(Vector2i.ZERO, false)
		d.run = false
		d.frame_dt = 1.0 / 60.0
		s.flush_held()
		var lr: Array = s.world().get_command_log().slice(lg)
		var row: int = 3 if case[1] < 0 else 0
		var seen2 := {}
		var bad := 0
		for e in lr:
			var ar: Array = e["command"]["area"]
			var idx := int(ar[0]) * cols + int(ar[1])
			if seen2.has(idx) or int(ar[0]) != row:
				bad += 1
			seen2[idx] = true
		var per_frame: float = s.walk_speed * s.run_factor * case[2]
		_ok("C3_hold_and_run_each_cell_once_%s" % case[3], lr.size() == cols and seen2.size() == cols and bad == 0,
				"commands=%d unique=%d bad=%d metres_per_frame=%.2f" % [lr.size(), seen2.size(), bad, per_frame])
	s.dispose()
	U.finish(self, fails)
