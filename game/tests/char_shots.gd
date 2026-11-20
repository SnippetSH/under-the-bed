# 캐릭터 화면 캡처 도구 (검사가 아니에요, ctest에 등록하지 않아요). 창을 띄워서(헤드리스 아님) 화면을 PNG로 저장해요.
#   godot --path game --script res://tests/char_shots.gd -- out=<폴더> [--resolution 1920x1080은 Godot 인자로]
# 저장 폴더는 user://char_shots_saves를 써서 실제 저장에 영향이 없어요.
# 찍는 것: 1 시작 화면, 2 낮(심어 둔 밭, 방금 한 일 표시·칸 정보), 3 밤, 4 장치 창(측창 조작판), 5 도움말, 6 출입문 창,
#          7 정식 미리 보기(빈 밭), 8 측창 연 모습·유동팬, 9 작기 종료 창.
extends SceneTree

const U = preload("res://tests/util.gd")
const Session = preload("res://scripts/session.gd")
const SaveStore = preload("res://scripts/save_store.gd")

var out_dir := ""


func shot(name: String) -> void:
	await process_frame
	await RenderingServer.frame_post_draw
	var img := root.get_texture().get_image()
	var path := out_dir.path_join(name)
	var e := img.save_png(path)
	print("shot %s %s size=%s" % [path, "ok" if e == OK else "err %d" % e, img.get_size()])


func frames(n: int) -> void:
	for i in range(n):
		await process_frame


func open_char(scenario_id: String) -> Node:
	Session.choose_scenario(scenario_id)
	var cp: Node = load("res://scenes/char_play.tscn").instantiate()
	root.add_child(cp)
	await frames(3)
	return cp


func close_char(cp: Node) -> void:
	cp.queue_free()
	await frames(2)


func _initialize() -> void:
	var args := U.parse_args()
	out_dir = String(args["out"])
	DirAccess.make_dir_recursive_absolute(out_dir)
	SaveStore.dir = "user://char_shots_saves"
	root.msaa_3d = Viewport.MSAA_4X
	if str(args.get("set", "")) == "v2":
		await shots_v2(args)
		Session.scenario_id = ""
		quit(0)
		return
	if str(args.get("set", "")) == "scale":
		await shots_scale()
		Session.scenario_id = ""
		quit(0)
		return

	# 1 시작 화면
	var st: Node = load("res://scenes/start.tscn").instantiate()
	root.add_child(st)
	await frames(4)
	await shot("01_start.png")
	st.queue_free()
	await frames(2)

	# 2 낮: 심어 둔 밭(처방 없이 시작), 한 달쯤 자란 뒤 오전 10시. 물뿌리개·비료 표시가 보이게
	var cp: Node = await open_char("char_planted")
	var s: Object = cp.s
	s.step_ticks(24 * 30 + 10)
	var L: Object = s.layout
	s.place(Vector2(L.aisle_center_x(1), 3.4), Vector2i(1, 0))
	s.select_tool(0)
	s.character_frame(Vector2i.ZERO, true, 0.016)
	s.character_frame(Vector2i.ZERO, false, 0.016)
	s.place(Vector2(L.aisle_center_x(1), 4.4), Vector2i(-1, 0))
	s.select_tool(1)
	s.character_frame(Vector2i.ZERO, true, 0.016)
	s.character_frame(Vector2i.ZERO, false, 0.016)
	s.place(Vector2(L.aisle_center_x(1), 5.4), Vector2i(1, 0))
	cp._notes.clear()
	await frames(10)
	await shot("02_day.png")

	# 3 밤 (21시)
	s.flush_held()
	while s.hour_now() != 21:
		s.skip_hour()
	s.place(Vector2(L.aisle_center_x(0), 6.4), Vector2i(1, 0))
	cp._notes.clear()
	await frames(10)
	await shot("03_night.png")

	# 4 장치 창: 측창 조작판 (다음 날 9시)
	while s.hour_now() != 9:
		s.skip_hour()
	var wp: Array = L.stand_point_for_device(L.device_by_id("window_panel"))
	s.place(wp[0], wp[1])
	s.character_frame(Vector2i.ZERO, true, 0.016)
	s.character_frame(Vector2i.ZERO, false, 0.016)
	cp._notes.clear()
	await frames(6)
	await shot("04_device_window_panel.png")
	cp._close_popup()

	# 5 도움말 (일시정지 메뉴 → 도움말 → 질소)
	cp._open_pause()
	cp.open_help(2)
	await frames(6)
	await shot("05_help.png")
	cp._open_pause()     # 도움말 → 일시정지 메뉴 → 계속 (화면에서 Esc 두 번과 같아요)
	cp._close_popup()

	# 6 출입문 창
	var dp: Array = L.stand_point_for_device(L.device_by_id("door"))
	s.place(dp[0], dp[1])
	s.character_frame(Vector2i.ZERO, true, 0.016)
	s.character_frame(Vector2i.ZERO, false, 0.016)
	await frames(6)
	await shot("06_door.png")
	cp._close_popup()

	# 8 측창 열고 유동팬 켜기, 가운데 통로에서 출입구 쪽 (낮 12시)
	while s.hour_now() != 12:
		s.skip_hour()
	for w in [[0, "left", 1.0], [1, "left", 1.0], [2, "left", 0.5], [0, "right", 1.0], [1, "right", 0.3]]:
		s.device_command({"action": "set_window", "layer": w[0], "side": w[1], "open_fraction": w[2]})
	s.device_command({"action": "set_fan", "on": true})
	s.skip_hour()
	s.place(Vector2(L.aisle_center_x(1), 8.0), Vector2i(0, -1))
	cp._notes.clear()
	await create_timer(1.5).timeout
	await shot("08_windows_fans.png")
	await close_char(cp)

	# 7 정식 미리 보기 (빈 밭, 오전 10시)
	var cp2: Node = await open_char("direct_play")
	var s2: Object = cp2.s
	s2.step_ticks(10)
	var sp: Array = s2.layout.stand_point_for_cell(45)
	s2.place(sp[0], sp[1])
	s2.device_command({"action": "plant", "seedling_grade": "B", "plants_per_m2": 8.0})
	cp2._notes.clear()
	await frames(10)
	await shot("07_planting_preview.png")
	# 9 작기 종료 창 (판 길이를 줄여서)
	s2.runner.season_ticks = s2.world().get_tick() + 2
	s2.skip_hour()
	s2.skip_hour()
	await frames(6)
	await shot("09_season_end.png")
	await close_char(cp2)
	Session.scenario_id = ""
	quit(0)


# ---------- set=v2 (딸기 단계 모델·런너·자러 가기 한 번 도는 빛, 2026-11-20 요청) ----------
const Cfg = preload("res://scripts/cfg.gd")
const Art = preload("res://scripts/plant_art.gd")
const Lib = preload("res://scripts/art_library.gd")


func _grow(scenario_id: String, days: int, hour := 10) -> Node:
	var cp: Node = await open_char(scenario_id)
	var s: Object = cp.s
	s.step_ticks(24 * days)
	while s.hour_now() != hour:
		s.step_ticks(1)
	cp._notes.clear()
	return cp


# 칸 c 옆 통로에 서서 그 칸을 바라봐요
func _stand_at_cell(s: Object, c: int) -> void:
	var sp: Array = s.layout.stand_point_for_cell(c)
	s.place(sp[0], sp[1])


func _stage_summary(cp: Node) -> String:
	var counts: Dictionary = cp.view.plant_instance_counts()
	var parts := []
	for k in counts:
		if int(counts[k]) > 0:
			parts.append("%s=%d" % [String(k).replace("strawberry_", ""), counts[k]])
	var t: Dictionary = cp.s.world().get_time()
	return "%d/%d %02d:00 runner_steps=%d %s" % [t["month"], t["day"], t["hour"], cp.view.runner_steps_now, ", ".join(parts)]


func _shot_grow(name: String, scenario_id: String, days: int, cell: int, close := false) -> void:
	var cp: Node = await _grow(scenario_id, days)
	_stand_at_cell(cp.s, cell)
	if close:
		cp.view._cam_h = 2.0
		cp.view._cam_d = 2.3
		cp.view._look = 0.9
	cp._notes.clear()
	await frames(8)
	print("%s: %s" % [name, _stage_summary(cp)])
	await shot(name)
	await close_char(cp)


func _dead_cell(s: Object) -> int:
	var v: Dictionary = s.world().get_tile_visuals(0.8)
	for i in range(v["crop_present"].size()):
		if v["crop_present"][i] == 1 and v["crop_alive"][i] == 0:
			return i
	return -1


func _frame_time(cp: Node, label: String, n := 300) -> void:
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	RenderingServer.viewport_set_measure_render_time(root.get_viewport_rid(), true)
	await frames(30)
	var t0 := Time.get_ticks_usec()
	var gpu := 0.0
	var cpu := 0.0
	for i in range(n):
		await process_frame
		gpu += RenderingServer.viewport_get_measured_render_time_gpu(root.get_viewport_rid())
		cpu += RenderingServer.viewport_get_measured_render_time_cpu(root.get_viewport_rid())
	var ms := (Time.get_ticks_usec() - t0) / 1000.0 / n
	print("FRAME %s size=%s avg_frame_ms=%.3f gpu_ms=%.3f render_cpu_ms=%.3f fps=%.0f [%s]" % [label, root.size, ms, gpu / n, cpu / n, 1000.0 / ms, _stage_summary(cp)])
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_ENABLED)


func shots_v2(args: Dictionary) -> void:
	# (a)~(g) 표준 작기(처방 일정표대로 키운 밭)
	await _shot_grow("v2_a_young.png", "season_standard", 2, 45)
	await _shot_grow("v2_b_vegetative.png", "season_standard", 30, 45)
	await _shot_grow("v2_c_flowering.png", "season_standard", 54, 45)
	await _shot_grow("v2_d_fruit_set.png", "season_standard", 66, 45)
	await _shot_grow("v2_e_harvest.png", "season_standard", 93, 45)
	# (f) 죽은 포기
	var cpf: Node = await _grow("season_standard", 100)
	var dc := _dead_cell(cpf.s)
	print("dead cell %d" % dc)
	_stand_at_cell(cpf.s, maxi(dc, 0))
	await frames(8)
	print("v2_f_dead_among_live.png: %s" % _stage_summary(cpf))
	await shot("v2_f_dead_among_live.png")
	await close_char(cpf)
	# (g) 캐릭터 옆 가까이 (카메라만 가까이)
	await _shot_grow("v2_g_close_with_character.png", "season_standard", 93, 45, true)
	# 정식 미리 보기 (빈 밭)
	var cp2: Node = await open_char("direct_play")
	cp2.s.step_ticks(10)
	_stand_at_cell(cp2.s, 45)
	cp2.s.device_command({"action": "plant", "seedling_grade": "B", "plants_per_m2": 8.0})
	cp2._notes.clear()
	await frames(8)
	print("v2_preview.png: %s" % _stage_summary(cp2))
	await shot("v2_preview.png")
	await close_char(cp2)
	# (i) 5월 중순 런너 (심어 둔 밭: 열매 몫이 있는 포기와 없는 포기가 섞여 둘 다 보여요), 그리고 표준 작기
	await _shot_grow("v2_i_mid_may_runners_char_planted.png", "char_planted", 247, 45)
	await _shot_grow("v2_i_mid_may_runners_close.png", "char_planted", 247, 45, true)
	await _shot_grow("v2_i_mid_may_runners_season_standard.png", "season_standard", 247, 45)
	# 프레임 시간: 꽃·열매가 가득한 때 (표준 작기 93일), 1920×1080
	var cpt: Node = await _grow("season_standard", 93)
	_stand_at_cell(cpt.s, 45)
	await _frame_time(cpt, "stage_models")
	await close_char(cpt)
	Lib.model_dir = "res://art/no_models_for_compare"
	Lib.clear_cache()
	var cpo: Node = await _grow("season_standard", 93)
	_stand_at_cell(cpo.s, 45)
	await _frame_time(cpo, "old_placeholders_with_addons")
	await close_char(cpo)
	Lib.model_dir = Lib.MODEL_DIR
	Lib.clear_cache()
	# (h) 30일 맡기기 한 번: 낮 14시에 시작, 표현 진행 0 / 0.2 / 0.4 / 0.6 / 0.8 / 끝
	var cph: Node = await _grow("season_standard", 60, 14)
	_stand_at_cell(cph.s, 45)
	await frames(8)
	var s: Object = cph.s
	await shot("v2_h0_skip_start.png")
	s.skip_days(30)
	var marks := [0.2, 0.4, 0.6, 0.8]
	var mi := 0
	var t0 := Time.get_ticks_msec()
	var levels := []
	while s.mode != "work":
		await process_frame
		levels.append(snappedf(cph.presented_light_level(), 0.001))
		if mi < marks.size() and s.rest_progress() >= marks[mi]:
			print("h%d progress=%.2f level=%.3f %s" % [mi + 1, s.rest_progress(), cph.presented_light_level(), _stage_summary(cph)])
			await shot("v2_h%d_skip_%d.png" % [mi + 1, int(marks[mi] * 100)])
			mi += 1
	await frames(4)
	print("h5 end after %.2f s level=%.3f %s" % [(Time.get_ticks_msec() - t0) / 1000.0, cph.presented_light_level(), _stage_summary(cph)])
	await shot("v2_h5_skip_end.png")
	print("skip levels (every 10th frame): %s" % [levels.filter(func(x): return true).slice(0, levels.size(), 10)])
	await close_char(cph)


# 크기 배율 비교 (stage_model_scale 0.4 / 0.5 / 0.6, 표준 작기 93일, 캐릭터 옆)
func shots_scale() -> void:
	for sc in [0.4, 0.5, 0.6]:
		Cfg._load().set_value("plant_models", "stage_model_scale", sc)
		await _shot_grow("scale_%d.png" % int(sc * 100), "season_standard", 93, 45)
		await _shot_grow("scale_%d_close.png" % int(sc * 100), "season_standard", 93, 45, true)
