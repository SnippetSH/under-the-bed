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
	out_dir = String(U.parse_args()["out"])
	DirAccess.make_dir_recursive_absolute(out_dir)
	SaveStore.dir = "user://char_shots_saves"
	root.msaa_3d = Viewport.MSAA_4X

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
