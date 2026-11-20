# 하우스 배치 (M6b 명세 3장): 그리드 크기 → 베드·통로·장치 자리. 그림과 걷는 길만 정해요(결과에 안 들어가요).
# 좌표는 바닥 평면 (x, z) 미터: x = 하우스 폭 방향(0행 쪽 벽 = x 0 = "좌"), z = 베드 길이 방향(0열 = 출입구 쪽, z 0).
# 출입구 쪽 빈 땅은 z < 0, 안쪽 끝 통로는 z > 베드 길이. 그리기 코드가 없어서 헤드리스 검사가 그대로 써요.
extends RefCounted

const Cfg = preload("res://scripts/cfg.gd")

var rows := 4
var cols := 20
var cell_m := 1.0
var aisle_m := 0.9
var front_m := 2.2
var back_m := 0.9
var device_depth_m := 0.45
var bed_count := 2
var aisle_count := 3
var width_m := 0.0
var length_m := 0.0
var bed_x0: Array = []     # 베드 b의 왼쪽 x
var bed_rows: Array = []   # 베드 b의 행 수 (2, 마지막은 홀수면 1)
var aisle_x0: Array = []   # 통로 k의 왼쪽 x
var devices: Array = []    # [{id, kind, label, rect: Rect2(x, z, w, d), ...}]


func setup(grid_rows: int, grid_cols: int, max_cover_layers: int) -> void:
	rows = grid_rows
	cols = grid_cols
	cell_m = float(Cfg.get_v("layout", "cell_m"))
	aisle_m = float(Cfg.get_v("layout", "aisle_m"))
	front_m = float(Cfg.get_v("layout", "front_zone_m"))
	back_m = float(Cfg.get_v("layout", "back_zone_m"))
	device_depth_m = float(Cfg.get_v("layout", "device_depth_m"))
	# 행 2개씩 묶어 베드 하나, 홀수면 마지막 베드는 폭 1칸 (명세 3장)
	bed_count = (rows + 1) / 2
	aisle_count = bed_count + 1
	bed_x0.clear()
	bed_rows.clear()
	aisle_x0.clear()
	var x := 0.0
	for b in range(bed_count):
		aisle_x0.append(x)
		x += aisle_m
		bed_x0.append(x)
		var n := mini(2, rows - 2 * b)
		bed_rows.append(n)
		x += n * cell_m
	aisle_x0.append(x)
	x += aisle_m
	width_m = x
	length_m = cols * cell_m
	_place_devices(max_cover_layers)


func _place_devices(max_layers: int) -> void:
	devices.clear()
	# 출입구 쪽 벽을 따라 한 줄: 난방기, 측창 조작판(난방기 옆), 유동팬, 점적 밸브, 출입문, 자동 물 주기 조절기, 작업대(모종), 작업대(피복)
	# P7: 벽마다 있던 측창 손잡이를 없애고 조작판 하나로 모든 겹·좌우를 다뤄요
	var kinds := [
		["heater", "난방기"], ["window_panel", "측창 조작판"], ["fan", "유동팬 스위치"], ["drip", "점적 밸브"], ["door", "출입문"],
		["auto_irrigation", "자동 물 주기"], ["bench_plant", "작업대: 모종 상자"], ["bench_cover", "작업대: 피복"],
	]
	var slot := width_m / kinds.size()
	var z0 := -front_m
	for i in range(kinds.size()):
		var w := slot * 0.78
		var cx := slot * (i + 0.5)
		devices.append({"id": kinds[i][0], "kind": kinds[i][0], "label": kinds[i][1],
				"rect": Rect2(cx - w * 0.5, z0, w, device_depth_m)})


# ---------- 칸·통로 ----------
func bed_of_row(r: int) -> int:
	return r / 2


func cell_rect(index: int) -> Rect2:
	var r := index / cols
	var c := index % cols
	var b := bed_of_row(r)
	return Rect2(float(bed_x0[b]) + (r - 2 * b) * cell_m, c * cell_m, cell_m, cell_m)


func cell_center(index: int) -> Vector2:
	return cell_rect(index).get_center()


func bed_rect(b: int) -> Rect2:
	return Rect2(float(bed_x0[b]), 0.0, int(bed_rows[b]) * cell_m, length_m)


func aisle_center_x(k: int) -> float:
	return float(aisle_x0[k]) + aisle_m * 0.5


# 서 있는 자리의 통로 번호 (베드 옆 구간만, 아니면 -1)
func aisle_at(p: Vector2) -> int:
	if p.y < 0.0 or p.y >= length_m:
		return -1
	for k in range(aisle_count):
		var x0: float = aisle_x0[k]
		if p.x >= x0 and p.x < x0 + aisle_m:
			return k
	return -1


# 통로 k의 왼쪽·오른쪽에 맞닿은 행 (없으면 -1)
func left_row_of_aisle(k: int) -> int:
	if k <= 0:
		return -1
	var b := k - 1
	return 2 * b + int(bed_rows[b]) - 1


func right_row_of_aisle(k: int) -> int:
	if k >= bed_count:
		return -1
	return 2 * k


# 대상 칸 (명세 4장): 서 있는 통로 칸과 맞닿은 베드 칸 가운데 바라보는 쪽의 칸 하나. 없으면 -1.
# 바라보는 방향의 x 성분이 0(통로를 따라 앞뒤)이면 어느 베드도 바라보지 않아서 대상이 없어요(구현자 판단).
func target_cell(p: Vector2, facing: Vector2i) -> int:
	var k := aisle_at(p)
	if k < 0 or facing.x == 0:
		return -1
	var col := clampi(int(floor(p.y / cell_m)), 0, cols - 1)
	var r := left_row_of_aisle(k) if facing.x < 0 else right_row_of_aisle(k)
	if r < 0:
		return -1
	return r * cols + col


# ---------- 걷기 ----------
func walkable(p: Vector2, radius: float) -> bool:
	if p.x < radius or p.x > width_m - radius:
		return false
	if p.y < -front_m + device_depth_m + radius or p.y > length_m + back_m - radius:
		return false
	for b in range(bed_count):
		if bed_rect(b).grow(radius).has_point(p):
			return false
	return true


# 바라보는 쪽으로 손 닿는 거리 안의 장치 (없으면 빈 사전)
func device_in_front(p: Vector2, facing: Vector2i, reach: float) -> Dictionary:
	if facing == Vector2i.ZERO:
		return {}
	var d := Vector2(facing).normalized()
	var steps := 10
	for i in range(1, steps + 1):
		var q := p + d * (reach * i / steps)
		for dev in devices:
			if (dev["rect"] as Rect2).grow(0.05).has_point(q):
				return dev
	return {}


func device_by_id(id: String) -> Dictionary:
	for dev in devices:
		if dev["id"] == id:
			return dev
	return {}


# 장치 앞에 서는 자리와 방향 (검사·자동 캡처용)
func stand_point_for_device(dev: Dictionary) -> Array:
	var r: Rect2 = dev["rect"]
	return [Vector2(r.get_center().x, r.end.y + 0.45), Vector2i(0, -1)]


# 칸 앞에 서는 자리와 방향: 그 행과 맞닿은 통로 중 하나 (짝수 행 = 왼쪽 통로)
func stand_point_for_cell(index: int) -> Array:
	var r := index / cols
	var c := index % cols
	var b := bed_of_row(r)
	var z := (c + 0.5) * cell_m
	if r == 2 * b:
		return [Vector2(aisle_center_x(b), z), Vector2i(1, 0)]
	return [Vector2(aisle_center_x(b + 1), z), Vector2i(-1, 0)]
