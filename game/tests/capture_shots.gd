# 화면 캡처 도구 (검사가 아니에요, ctest에 등록하지 않아요). 창을 띄워서(헤드리스 아님) 화면을 PNG로 저장해요.
#   godot --path game --script res://tests/capture_shots.gd -- out=<폴더>
# 저장 폴더는 user://shots_saves를 써서 실제 저장에 영향이 없어요.
# 찍는 것: 1 시작 화면, 2 작기 중반 격자, 3 행동 패널(지금은 안 되는 행동 / 되는 행동), 4 저장 목록, 5 개발자 패널(오버레이),
#          6 직접 플레이 정식 뒤, 7 작기 종료 화면.
extends SceneTree

const U = preload("res://tests/util.gd")
const Session = preload("res://scripts/session.gd")
const SaveStore = preload("res://scripts/save_store.gd")
const SimRunner = preload("res://scripts/sim_runner.gd")
const Cfg = preload("res://scripts/cfg.gd")

var out_dir := ""


func shot(name: String) -> void:
	await process_frame
	await RenderingServer.frame_post_draw
	var img := root.get_viewport().get_texture().get_image()
	var path := out_dir.path_join(name)
	var e := img.save_png(path)
	print("shot %s %s size=%s" % [path, "ok" if e == OK else "err %d" % e, img.get_size()])


func frames(n: int) -> void:
	for i in range(n):
		await process_frame


func make_runner(id: String) -> Node:
	var r: Node = SimRunner.new()
	r.load_config()
	r.start_new(Cfg.scenario(id))
	return r


func _initialize() -> void:
	out_dir = String(U.parse_args()["out"])
	DirAccess.make_dir_recursive_absolute(out_dir)
	SaveStore.dir = "user://shots_saves"
	var abs_dir := ProjectSettings.globalize_path(SaveStore.dir)
	DirAccess.make_dir_recursive_absolute(abs_dir)
	for f in DirAccess.get_files_at(abs_dir):
		DirAccess.open(abs_dir).remove(f)

	# 저장 목록용 파일: 정상 둘, 다른 버전 하나, 깨진 것 하나
	var a := make_runner("season_standard")
	a.autosave_enabled = false
	a.step(800)
	SaveStore.save(a, "slot_1")
	var d := make_runner("direct_play")
	d.autosave_enabled = false
	d.step(300)
	SaveStore.save(d, SaveStore.QUICK)
	var u := SaveStore.unpack(SaveStore.build_bundle(a, "manual"))
	var core: PackedByteArray = u["core"].duplicate()
	core[4] = SimWorld.core_save_version() - 1
	SaveStore.write_bytes(SaveStore.pack(u["summary"], u["game"], u["engine"], core), "slot_3")
	var good := SaveStore.build_bundle(a, "manual")
	SaveStore.write_bytes(good.slice(0, good.size() / 3), "slot_4")
	a.free()
	d.free()

	var start: Node = load("res://scenes/start.tscn").instantiate()
	root.add_child(start)
	await frames(3)
	await shot("1_start.png")
	start.free()

	# 자동 처방 관전, 작기 중반
	Session.choose_scenario("season_standard")
	var main: Node = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)
	await frames(2)
	main.runner.step(24 * 120)
	main.bed.selection = [0, 2, 2, 6]
	main.bed.selection_changed.emit(main.bed.selection)
	main.dev.cell_index = 22
	main.bed.picked_cell = 22
	main._dirty = true
	await frames(3)
	main.dev.refresh()
	await shot("2_midseason.png")

	main.action_panel.select_action("plant")
	await frames(2)
	main.action_panel.refresh_availability()
	main.action_panel.run_selected()  # 거부 알림
	await frames(2)
	await shot("3a_action_blocked.png")
	main.action_panel.select_action("add_fertilizer")
	await frames(2)
	main.action_panel.refresh_availability()
	await shot("3b_action_ok.png")

	main.bed.clear_selection()
	main._toggle_save_panel()
	await frames(2)
	await shot("4_save_list.png")
	main._toggle_save_panel()

	for i in range(main.dev._options.size()):
		if main.dev._options[i]["field"] == "water_mm":
			main.dev._overlay_opt.select(i)
	main.dev.refresh()
	await frames(2)
	await shot("5_dev_overlay.png")
	main.dev._overlay_opt.select(0)

	# 종료 화면
	main.runner.step(6312 - main.runner.world.get_tick())
	await frames(3)
	await shot("7_end_screen.png")
	main.free()

	# 직접 플레이: 정식 뒤 60일
	Session.choose_scenario("direct_play")
	var m2: Node = load("res://scenes/main.tscn").instantiate()
	root.add_child(m2)
	await frames(2)
	m2.runner.autosave_enabled = false
	m2.runner.submit({"action": "plant", "seedling_grade": "B", "plants_per_m2": 8.0})
	m2.runner.step(24 * 60)
	m2.bed.select_all()
	m2.action_panel.select_action("set_mulch")
	await frames(2)
	await shot("6_direct_play.png")
	m2.free()
	await frames(2)
	quit(0)
