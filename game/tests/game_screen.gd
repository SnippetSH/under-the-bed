# G3: 점검 화면 로직 검사 (진짜 main.tscn / start.tscn 장면을 헤드리스로 띄워요).
#  - 행동 패널: 17종이 다 있고, can_enqueue로 지금 안 되는 행동이 흐려짐(직접 플레이 시작 때와 정식 뒤가 다름),
#    화면 경로(select_action -> run_selected)로 17종 전부 넣어 봐서 결과가 나옴, 거부는 알림이 뜸 (H2·H3)
#  - 뽑은 칸은 안 심은 칸과 똑같이 보임 (F2), 포기가 죽었다는 알림에 원인이 없음 (F4)
#  - 개발자 패널: 오버레이가 칸 수만큼 칠해지고, 패널을 끄면 오버레이도 꺼짐 (H8)
#  - 시작 화면이 시나리오 목록과 저장 목록을 만듦
# 만든 노드는 끝에 free()해요.
#   godot --headless --path game --script res://tests/game_screen.gd -- out=<폴더>
extends SceneTree

const U = preload("res://tests/util.gd")
const Session = preload("res://scripts/session.gd")
const SaveStore = preload("res://scripts/save_store.gd")
const SimRunner = preload("res://scripts/sim_runner.gd")
const Cfg = preload("res://scripts/cfg.gd")

const EXPECTED_ACTIONS := ["irrigate", "add_fertilizer", "plant", "set_window", "set_heater", "set_fan", "set_cover_layers",
	"install_drip", "add_compost", "add_lime", "till", "set_mulch", "apply_disinfectant", "add_inoculant", "thin_flowers",
	"remove_plant", "release_predators"]

var fails := PackedStringArray()


func check(cond: bool, what: String) -> void:
	if not cond:
		fails.push_back(what)


func notes_text(main: Node) -> String:
	return " | ".join(main._notes.map(func(n): return n["text"]))


func _initialize() -> void:
	SaveStore.dir = String(U.parse_args()["out"]).path_join("g3_saves")
	var abs_dir := ProjectSettings.globalize_path(SaveStore.dir)
	DirAccess.make_dir_recursive_absolute(abs_dir)
	for f in DirAccess.get_files_at(abs_dir):
		DirAccess.open(abs_dir).remove(f)
	await run_main_checks()
	await run_start_checks()
	U.finish(self, fails)


func run_main_checks() -> void:
	Session.choose_scenario("direct_play")
	var main: Node = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)
	await process_frame
	await process_frame
	var ap: Node = main.action_panel
	var runner: Node = main.runner
	var bed: Control = main.bed
	var dev: Node = main.dev
	var ids: Array = ap.action_ids()
	check(ids.size() == 17 and EXPECTED_ACTIONS.all(func(id): return id in ids), "action list should hold the 17 actions: %s" % [ids])

	# 직접 플레이 시작: 빈 밭이라 정식은 되고, 뽑을 포기·점적(이미 있음)·피복 겹수(휴작기 밖)는 안 돼요
	ap.refresh_availability()
	check(ap.is_available("plant"), "plant should be available on the empty field")
	check(not ap.is_available("remove_plant"), "remove_plant should be greyed on a field with no crop")
	check(not ap.is_available("install_drip"), "install_drip should be greyed (drip is already installed)")
	check(not ap.is_available("set_cover_layers"), "set_cover_layers should be greyed outside the off-season")
	check(ap.is_available("irrigate") and ap.is_available("set_fan"), "irrigate and set_fan should be available")
	check(not runner.world.is_schedule_enabled() and runner.auto_irrigation["enabled"], "direct play: schedule off, auto irrigation on")

	# 정식 (화면 경로)
	var n_log0: int = runner.world.get_command_log().size()
	ap.select_action("plant")
	ap.run_selected()
	check(runner.world.get_pending_command_count() == 1, "plant should be queued")
	runner.step(1)
	ap.refresh_availability()
	check(not ap.is_available("plant"), "plant should be greyed once a live crop exists")
	check(ap.is_available("remove_plant"), "remove_plant should be available once there are plants")
	# 거부: 같은 정식을 다시 넣으면 알림 (H3)
	ap.select_action("plant")
	ap.run_selected()
	check("거부" in notes_text(main) and "정식" in notes_text(main), "a rejected plant should raise a notice: %s" % notes_text(main))
	check(runner.world.get_pending_command_count() == 0, "a rejected command must not be queued")

	# 영역 행동인데 영역이 없으면 안내
	main._notes.clear()
	ap.select_action("till")
	ap.run_selected()
	check("영역" in notes_text(main), "an area action without a selection should ask for an area")

	# 17종 전부 화면 경로로 (전체 선택). 결과 사전이 나오고, 받아들여진 것은 대기 명령이 돼요
	bed.select_all()
	var results := {}
	ap.submitted.connect(func(a: Dictionary, r: Dictionary): results[a["id"]] = r)
	for id in EXPECTED_ACTIONS:
		check(ap.select_action(id), "select_action %s" % id)
		ap.run_selected()
	check(results.size() == 17, "all 17 actions should produce a result, got %d" % results.size())
	var accepted := 0
	var rejected_ids: Array = []
	for id in results:
		if results[id]["ok"]:
			accepted += 1
		else:
			rejected_ids.append(id)
	rejected_ids.sort()
	print("G3 submitted 17 actions: accepted=%d rejected=%s" % [accepted, rejected_ids])
	check(rejected_ids == ["install_drip", "plant", "set_cover_layers"], "only drip, plant and cover layers should be rejected here: %s" % [rejected_ids])
	check(runner.world.get_pending_command_count() == accepted, "pending count should equal the accepted commands")
	runner.step(2)
	check(runner.world.get_command_log().size() >= n_log0 + accepted, "accepted commands should be in the log")

	# F4: 죽었다는 알림에 원인이 없어요
	main._notes.clear()
	main._on_events([{"tick": 5, "type": "plant_died", "tile_index": 3, "cause": "waterlog"}])
	check("죽었어요" in notes_text(main) and "waterlog" not in notes_text(main), "death notice should not name the cause: %s" % notes_text(main))

	# F2: 뽑은 칸은 안 심은 칸과 같아 보여요
	bed.select_all()
	ap.select_action("remove_plant")
	ap.run_selected()
	runner.step(1)
	var after_remove: Dictionary = runner.world.get_tile_visuals(0.8)
	var fresh: SimWorld = U.new_world("res://data/params.toml", Cfg.scenario("direct_play")["path"], false)
	var fresh_vis: Dictionary = fresh.get_tile_visuals(0.8)
	var same := true
	for k in ["crop_present", "crop_alive", "leaf_g_per_m2", "open_flower_count", "fruit_count", "ripening_fruit_count"]:
		same = same and after_remove[k] == fresh_vis[k]
	check(same, "removed-plant cells should look like never-planted cells")

	# 개발자 패널 (H8): 오버레이는 패널이 켜져 있을 때만
	check(dev.visible, "dev panel should be on by default in M6")
	var water_idx := -1
	for i in range(dev._options.size()):
		if dev._options[i]["field"] == "water_mm":
			water_idx = i
	check(water_idx > 0, "overlay options should include water_mm")
	dev._overlay_opt.select(water_idx)
	dev.refresh()
	check(bed.overlay.size() == 80 and dev._legend.text != "", "overlay should cover all 80 cells, legend: %s" % dev._legend.text)
	dev.visible = false
	check(bed.overlay.size() == 0, "hiding the dev panel must clear the overlay")
	dev.visible = true
	var hash_label_before: String = dev._hash_label.text
	dev._on_hash()
	check(dev._hash_label.text != hash_label_before and "0x" in dev._hash_label.text, "the hash button should show a hash")
	main.queue_free()
	await process_frame


func run_start_checks() -> void:
	# 저장 하나 만들어 두고 시작 화면 목록 확인
	var runner: Node = SimRunner.new()
	runner.load_config()
	runner.start_new(Cfg.scenario("season_standard"))
	runner.step(24)
	check(SaveStore.save(runner, "slot_1")["ok"], "start: save for the list")
	runner.free()
	var start: Node = load("res://scenes/start.tscn").instantiate()
	root.add_child(start)
	await process_frame
	var texts: Array = start.find_children("*", "Button", true, false).map(func(b): return (b as Button).text)
	for id in Cfg.scenario_ids():
		check(Cfg.scenario(str(id))["label"] in texts, "start screen should list scenario %s" % id)
	check("불러오기" in texts and texts.any(func(t): return t.begins_with("이어하기 (가장")), "start screen should offer loading and continuing")
	start.queue_free()
	await process_frame
