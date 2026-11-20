# C14 (m6b-character-play 9.1, 2.1·3장): 피복·측창·유동팬·바깥. 겹수와 측창 단계를 바꾸고 팬을 켜요.
#  A) 피복은 겹수만큼 그려지고(바뀌면 다시 만듦), 측창은 겹마다 좌우가 열린 만큼 말려 올라가며 부드럽게 움직여요(R7).
#     측창 모양은 벽을 따라 길고, 통로·베드 쪽으로 튀어나오지 않아요(B2)
#  B) 실제 판: 측창 명령 → 다음 시간 → 화면의 피복 그림이 코어 값(get_house_controls)을 따라감
#  C) 천장 유동팬 5개가 가운데 한 줄에 고른 간격(R21), 켜면 돌고 끄면 서서히 멈춤, 걷거나 달릴 때 위치에 따라 서서히
#     투명해짐(튀지 않음), 유동팬 스위치 앞에서 입구 쪽 팬이 화면 안에 또렷이 보임
#  D) 바깥 땅·하늘·안개(R22): 땅 끝이 어디서 봐도 안개 너머, 하늘은 그라데이션 하늘
#  E) "방금 한 일" 표시·대상 칸 테두리는 깊이 검사를 해요(캐릭터를 가리지 않음, B1)
#   godot --headless --path game --script res://tests/c14_cover_fans_outside.gd -- out=<폴더>
extends SceneTree

const U = preload("res://tests/util.gd")
const Driver = preload("res://tests/char_driver.gd")
const HouseCover = preload("res://scripts/house_cover.gd")
const HouseLayout = preload("res://scripts/house_layout.gd")
const HouseView3D = preload("res://scripts/house_view_3d.gd")
const SaveStore = preload("res://scripts/save_store.gd")

const DT := 1.0 / 60.0
const MAX_FADE_STEP := 0.05   # 한 프레임(1/60초)에 팬 불투명도가 바뀔 수 있는 최대값

var fails := PackedStringArray()


func _ok(name: String, cond: bool, detail := "") -> void:
	if not cond:
		fails.push_back("%s %s" % [name, detail])
	print("%s %s %s" % ["ok  " if cond else "FAIL", name, detail])


func _hc(n: int, wf: Array) -> Dictionary:
	return {"cover_layers": n, "window_open_fraction": PackedFloat64Array(wf)}


func _aabb(mi: MeshInstance3D) -> AABB:
	return mi.transform * mi.mesh.get_aabb()


func _initialize() -> void:
	var args := U.parse_args()
	SaveStore.dir = String(args["out"]).path_join("c14_saves")
	await process_frame
	part_a()
	await part_bcde()
	U.finish(self, fails)


func part_a() -> void:
	var L := HouseLayout.new()
	L.setup(4, 20, 4)
	var c: Node3D = HouseCover.new()
	get_root().add_child(c)
	c.setup(L)
	c.sync(_hc(2, [0.0, 0.0, 0.0, 0.0]), 0.016)
	_ok("A_two_layers_built", c.layers.size() == 2)
	c.sync(_hc(4, [0, 0, 0, 0, 0, 0, 0, 0]), 0.016)
	_ok("A_rebuilt_to_four_layers", c.layers.size() == 4)
	c.sync(_hc(3, [0.0, 0.3, 0.5, 0.7, 1.0, 0.0]), 0.016)
	_ok("A_rebuilt_to_three_layers_starts_at_state", c.layers.size() == 3 and is_equal_approx(c.shown(1, 1), 0.7) and is_equal_approx(c.shown(2, 0), 1.0))
	var seq := []
	for i in range(8):
		c.sync(_hc(3, [1.0, 0.3, 0.5, 0.7, 1.0, 0.0]), 0.1)
		seq.append(snappedf(c.shown(0, 0), 0.001))
	var mono := true
	for i in range(1, seq.size()):
		if seq[i] < seq[i - 1]:
			mono = false
	_ok("A_window_animates_smoothly_to_target", mono and seq[0] > 0.0 and seq[0] < 0.2 and is_equal_approx(seq[5], 1.0) and is_equal_approx(seq[7], 1.0), str(seq))
	_ok("A_other_windows_unchanged_during_anim", is_equal_approx(c.shown(0, 1), 0.3) and is_equal_approx(c.shown(2, 1), 0.0))
	var w_open: MeshInstance3D = c.layers[0]["win"][0]
	var w_closed: MeshInstance3D = c.layers[2]["win"][1]
	_ok("A_fully_open_film_rolled_away", not w_open.visible)
	_ok("A_closed_film_full_height", w_closed.visible and is_equal_approx(w_closed.scale.y, c.wall_h - c.sill) and is_equal_approx(w_closed.position.y, c.sill))
	c.sync(_hc(3, [0.0, 0.3, 0.5, 0.7, 1.0, 0.0]), 0.1)
	var after_flip: float = c.shown(0, 0)
	_ok("A_reverse_mid_anim_continues_from_current", after_flip > 0.9 and after_flip < 1.0, str(after_flip))
	_ok("A_layers_spaced_inward", c.layers[2]["win"][0].position.x > c.layers[0]["win"][0].position.x + 0.1)
	# 측창 모양: 벽을 따라 길게, 통로·베드 쪽으로 튀어나오지 않음
	c.sync(_hc(4, [1.0, 1.0, 0.5, 1.0, 0.0, 1.0, 0.3, 0.7]), 10.0)
	var bad := []
	var aisle0_mid := L.aisle_center_x(0)
	var aisleN_mid := L.aisle_center_x(L.aisle_count - 1)
	var wall_len: float = c.z1 - c.z0
	for k in range(4):
		for side in [0, 1]:
			for part in ["win", "roll"]:
				var mi: MeshInstance3D = c.layers[k][part][side]
				if not mi.visible:
					continue
				var bb := _aabb(mi)
				var inward_ok: bool = (bb.end.x < aisle0_mid) if side == 0 else (bb.position.x > aisleN_mid)
				var runs_along: bool = bb.size.z > wall_len * 0.95
				var low_enough: bool = bb.end.y <= c.wall_h + 0.1 and bb.position.y >= 0.0
				var slim: bool = part != "roll" or (bb.size.x < 0.12 and bb.size.y < 0.12)
				if not (inward_ok and runs_along and low_enough and slim):
					bad.append("%d%s %s %s" % [k, "L" if side == 0 else "R", part, bb])
	_ok("A_window_parts_run_along_wall_and_stay_out_of_aisles", bad.is_empty(), str(bad))
	c.free()


func part_bcde() -> void:
	var s: Object = Driver.new_session("direct_play", false)
	var view: Node3D = HouseView3D.new()
	get_root().add_child(view)
	await process_frame
	view.setup(s)
	var L: Object = s.layout
	# ---- B) 실제 판: 측창 명령 → 다음 시간 → 그림이 코어 값을 따라감 ----
	var hc0: Dictionary = s.world().get_house_controls()
	_ok("B_cover_drawn_with_core_layer_count", view.cover.layers.size() == int(hc0["cover_layers"]), "%d vs %d" % [view.cover.layers.size(), hc0["cover_layers"]])
	for w in [[0, "left", 1.0], [1, "right", 0.5]]:
		s.device_command({"action": "set_window", "layer": w[0], "side": w[1], "open_fraction": w[2]})
	s.device_command({"action": "set_fan", "on": true})
	s.step_ticks(1)
	for i in range(90):
		view.update_dynamic(DT)
	var hc1: Dictionary = s.world().get_house_controls()
	var arr: PackedFloat64Array = hc1["window_open_fraction"]
	var follows := true
	for k in range(int(hc1["cover_layers"])):
		for side in [0, 1]:
			if not is_equal_approx(view.cover.shown(k, side), arr[k * 2 + side]):
				follows = false
	_ok("B_windows_drawn_as_core_state_after_next_hour", follows and is_equal_approx(view.cover.shown(0, 0), 1.0) and is_equal_approx(view.cover.shown(1, 1), 0.5), str(arr))
	# ---- C) 유동팬 ----
	_ok("C_five_fans", view._fans.size() == 5, "fans=%d" % view._fans.size())
	var xs: Array = view._fans.map(func(f): return float(f["root"].position.x))
	var zs: Array = view._fans.map(func(f): return float(f["root"].position.z))
	var on_line := true
	for xx in xs:
		if not is_equal_approx(xx, L.width_m * 0.5):
			on_line = false
	var even := true
	for i in range(2, zs.size()):
		if absf((zs[i] - zs[i - 1]) - (zs[1] - zs[0])) > 0.001:
			even = false
	_ok("C_all_fans_on_centre_line_evenly_spaced", on_line and even and zs[0] < 0.0 and zs[0] > -L.front_m, "x=%s z=%s" % [xs, zs])
	var r0: float = view._fans[0]["blades"].rotation.z
	view.update_dynamic(DT)
	_ok("C_fans_spin_when_on", bool(hc1["fan_on"]) and view._fans[0]["blades"].rotation.z != r0 and float(view.fan_spin_speeds()[0]) > 0.0, str(view.fan_spin_speeds()))
	for run in [false, true]:
		s.place(Vector2(L.aisle_center_x(1), -1.0), Vector2i(0, 1))
		view.update_dynamic(DT)
		var prev: Array = view.fan_opacities()
		var max_step := 0.0
		var seen_full := {}
		var seen_gone := {}
		var frames := 0
		for dir in [1, -1]:
			for i in range(3000):
				var z0: float = s.pos.y
				s.character_frame(Vector2i(0, dir), false, DT, run)
				view.update_dynamic(DT)
				frames += 1
				var cur: Array = view.fan_opacities()
				for k in range(cur.size()):
					max_step = maxf(max_step, absf(float(cur[k]) - float(prev[k])))
					if float(cur[k]) > 0.999:
						seen_full[k] = true
					if float(cur[k]) < 0.001:
						seen_gone[k] = true
				prev = cur
				if s.pos.y == z0:
					break
		var both := 0
		for k in seen_full:
			if seen_gone.has(k):
				both += 1
		var tag := "run" if run else "walk"
		_ok("C_%s_fan_fade_no_jumps" % tag, max_step <= MAX_FADE_STEP, "max change per frame=%.4f over %d frames" % [max_step, frames])
		_ok("C_%s_bed_fans_fade_out_and_back_in" % tag, both >= 4, "fans fully visible and fully gone: %d" % both)
	var sw: Dictionary = L.device_by_id("fan")
	var sp: Array = L.stand_point_for_device(sw)
	s.place(sp[0], sp[1])
	view.update_dynamic(DT)
	_ok("C_standing_at_switch_faces_it", s.device_in_front().get("id", "") == "fan")
	var ent: Dictionary = view._fans[0]
	var gp: Vector3 = (ent["root"] as Node3D).global_position
	_ok("C_entrance_fan_on_screen_and_opaque_at_switch", view.camera.is_position_in_frustum(gp) and float(ent["opacity"]) > 0.999, "opacity=%.2f pos=%s" % [ent["opacity"], gp])
	for i in range(120):
		view.update_dynamic(DT)
	var on_speed: float = float(ent["speed"])
	s.device_command({"action": "set_fan", "on": false})
	s.step_ticks(1)
	for i in range(30):
		view.update_dynamic(DT)
	var slowing: float = float(ent["speed"])
	for i in range(120):
		view.update_dynamic(DT)
	_ok("C_fan_spins_then_slows_then_stops", on_speed > 0.0 and slowing > 0.0 and slowing < on_speed and float(ent["speed"]) == 0.0, "on=%.1f after0.5s=%.1f after2.5s=%.1f" % [on_speed, slowing, ent["speed"]])
	# ---- D) 바깥 ----
	var g: MeshInstance3D = view.ground
	var bb: AABB = g.transform * g.mesh.get_aabb()
	var d99: float = log(100.0) / view.env.fog_density   # 지수 안개가 99 % 가리는 거리
	var worst := INF
	for p in [Vector2(0.3, -L.front_m), Vector2(L.width_m - 0.3, -L.front_m), Vector2(0.3, L.length_m + L.back_m), Vector2(L.width_m - 0.3, L.length_m + L.back_m)]:
		s.place(p, Vector2i(0, -1))
		view.update_dynamic(DT)
		var cpos: Vector3 = view.camera.global_position
		worst = minf(worst, minf(minf(cpos.x - bb.position.x, bb.end.x - cpos.x), minf(cpos.z - bb.position.z, bb.end.z - cpos.z)))
	_ok("D_outside_ground_edge_beyond_fog_from_all_corners", worst > d99, "nearest ground edge %.0f m, fog hides beyond %.0f m" % [worst, d99])
	_ok("D_ground_within_camera_far", view.camera.far > d99, "camera far %.0f m" % view.camera.far)
	_ok("D_sky_is_gradient_sky_with_fog", view.env.background_mode == Environment.BG_SKY and view.sky_mat != null and view.env.fog_enabled)
	# ---- E) 표시의 깊이 검사 ----
	s.place(Vector2(L.aisle_center_x(1), 3.5), Vector2i(1, 0))
	s.select_tool(0)
	s.use_tool_on(s.target_cell())
	view.update_dynamic(DT)
	var markers_depth: bool = not view._markers.is_empty()
	for m in view._markers:
		if (m as Sprite3D).no_depth_test:
			markers_depth = false
	_ok("E_markers_and_target_outline_depth_tested", markers_depth and not view._frame.no_depth_test and not view._char_shadow.no_depth_test)
	view.free()
	s.dispose()
