# C1 (m6b-character-play 9.1): 대상 칸 규칙 (명세 4장). 통로 칸·방향을 정해 놓고 대상 칸을 물어요
# (베드 양쪽, 끝, 맞닿은 칸 없음, 홀수 행). 덤: 80칸 모두 서는 자리에서 대상이 되는지(G2 자동판), 장치 앞 판정.
#   godot --headless --path game --script res://tests/c01_target_cell.gd
extends SceneTree

const U = preload("res://tests/util.gd")
const HouseLayout = preload("res://scripts/house_layout.gd")

var fails := PackedStringArray()


func _check(name: String, got: int, want: int) -> void:
	var ok := got == want
	if not ok:
		fails.push_back("%s got=%d want=%d" % [name, got, want])
	print("%s %s got=%d want=%d" % ["ok  " if ok else "FAIL", name, got, want])


func _init() -> void:
	U.parse_args()
	var L: HouseLayout = HouseLayout.new()
	L.setup(4, 20, 4)
	var cols := 20
	var z5 := 5.5   # 5열 가운데
	var a0 := L.aisle_center_x(0)
	var a1 := L.aisle_center_x(1)
	var a2 := L.aisle_center_x(2)
	print("layout width_m=%.2f length_m=%.1f beds=%d aisles=%d" % [L.width_m, L.length_m, L.bed_count, L.aisle_count])
	# 왼쪽 벽 통로: 오른쪽 = 0행, 왼쪽 = 벽 (없음)
	_check("left_wall_aisle_face_right", L.target_cell(Vector2(a0, z5), Vector2i(1, 0)), 0 * cols + 5)
	_check("left_wall_aisle_face_left_none", L.target_cell(Vector2(a0, z5), Vector2i(-1, 0)), -1)
	# 가운데 통로: 왼쪽 = 1행(왼쪽 베드의 오른쪽 줄), 오른쪽 = 2행(오른쪽 베드의 왼쪽 줄)
	_check("middle_aisle_face_left", L.target_cell(Vector2(a1, z5), Vector2i(-1, 0)), 1 * cols + 5)
	_check("middle_aisle_face_right", L.target_cell(Vector2(a1, z5), Vector2i(1, 0)), 2 * cols + 5)
	# 오른쪽 벽 통로
	_check("right_wall_aisle_face_left", L.target_cell(Vector2(a2, z5), Vector2i(-1, 0)), 3 * cols + 5)
	_check("right_wall_aisle_face_right_none", L.target_cell(Vector2(a2, z5), Vector2i(1, 0)), -1)
	# 대각선은 옆 성분만 봐요 (같은 열)
	_check("diagonal_forward_right", L.target_cell(Vector2(a1, z5), Vector2i(1, 1)), 2 * cols + 5)
	_check("diagonal_back_left", L.target_cell(Vector2(a1, z5), Vector2i(-1, -1)), 1 * cols + 5)
	# 통로를 따라 앞뒤를 보면 없음
	_check("face_along_aisle_none", L.target_cell(Vector2(a1, z5), Vector2i(0, 1)), -1)
	_check("face_back_along_aisle_none", L.target_cell(Vector2(a1, z5), Vector2i(0, -1)), -1)
	# 베드 끝: 0열, 19열, 그 바깥(출입구 쪽 빈 땅, 안쪽 끝 통로)
	_check("bed_end_col0", L.target_cell(Vector2(a1, 0.05), Vector2i(1, 0)), 2 * cols + 0)
	_check("bed_end_col19", L.target_cell(Vector2(a1, 19.95), Vector2i(-1, 0)), 1 * cols + 19)
	_check("front_zone_none", L.target_cell(Vector2(a1, -0.3), Vector2i(1, 0)), -1)
	_check("back_zone_none", L.target_cell(Vector2(a1, 20.3), Vector2i(1, 0)), -1)
	# 홀수 행(3행): 마지막 베드는 폭 1칸, 양쪽 통로 모두 그 줄과 맞닿아요
	var L3: HouseLayout = HouseLayout.new()
	L3.setup(3, 20, 4)
	_check("odd_rows_aisle1_right", L3.target_cell(Vector2(L3.aisle_center_x(1), z5), Vector2i(1, 0)), 2 * cols + 5)
	_check("odd_rows_aisle2_left", L3.target_cell(Vector2(L3.aisle_center_x(2), z5), Vector2i(-1, 0)), 2 * cols + 5)
	# G2 자동판: 80칸 모두 "서는 자리"가 걸을 수 있는 곳이고 그 자리에서 대상 칸이 그 칸이에요
	var bad := 0
	for i in range(80):
		var sp: Array = L.stand_point_for_cell(i)
		if not L.walkable(sp[0], 0.22) or L.target_cell(sp[0], sp[1]) != i:
			bad += 1
	_check("all_80_cells_reachable_and_targeted_bad_count", bad, 0)
	# 장치: 출입문 앞에서 출입구 쪽을 보면 출입문, 측창 조작판은 난방기 옆 하나(벽 손잡이 없음, R7)
	var door: Dictionary = L.device_by_id("door")
	var sp_d: Array = L.stand_point_for_device(door)
	_check("door_in_front", 1 if L.device_in_front(sp_d[0], sp_d[1], 0.9).get("id", "") == "door" else 0, 1)
	var win: Dictionary = L.device_by_id("window_panel")
	var sp_w: Array = L.stand_point_for_device(win)
	_check("window_panel_in_front", 1 if L.device_in_front(sp_w[0], sp_w[1], 0.9).get("id", "") == "window_panel" else 0, 1)
	_check("window_panel_target_cell_none", L.target_cell(sp_w[0], sp_w[1]), -1)
	var heater_r: Rect2 = L.device_by_id("heater")["rect"]
	_check("window_panel_next_to_heater", 1 if absf((win["rect"] as Rect2).get_center().x - heater_r.get_center().x) < 1.0 else 0, 1)
	_check("no_wall_window_handles", L.devices.filter(func(dv): return dv["kind"] == "window").size(), 0)
	_check("left_wall_nothing_in_front", 1 if L.device_in_front(Vector2(a0, 3.0), Vector2i(-1, 0), 0.9).is_empty() else 0, 1)
	# 장치 8종(출입문 포함)이 모두 있고 각자 서는 자리에서 그 장치를 봐요 (G3 자동판)
	var ids := ["window_panel", "heater", "fan", "bench_cover", "drip", "bench_plant", "auto_irrigation", "door"]
	var dev_bad := 0
	for id in ids:
		var dv: Dictionary = L.device_by_id(id)
		if dv.is_empty():
			dev_bad += 1
			continue
		var sp: Array = L.stand_point_for_device(dv)
		if not L.walkable(sp[0], 0.22) or L.device_in_front(sp[0], sp[1], 0.9).get("id", "") != id:
			dev_bad += 1
	_check("all_8_devices_reachable_bad_count", dev_bad, 0)
	U.finish(self, fails)
