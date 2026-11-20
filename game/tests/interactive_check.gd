# 창을 띄워서(헤드리스 아님) 합성 입력으로 점검 화면을 조작해 보는 도구 (검사가 아니에요, ctest에 등록하지 않아요).
# 마우스 끌기 영역 선택, 키(1~4, Space, F3, F5, F9), 자동 물 주기 입력칸, 실행 단추, 그리고 가장 빠른 배속으로 실제 프레임을 돌려
# 작기가 끝나는지(종료 화면 해시)와 프레임 시간을 찍어요.
#   godot --path game --script res://tests/interactive_check.gd -- out=<폴더>
extends SceneTree

const U = preload("res://tests/util.gd")
const Session = preload("res://scripts/session.gd")
const SaveStore = preload("res://scripts/save_store.gd")

var main: Node


func mouse(pos: Vector2, pressed: bool) -> void:
	var ev := InputEventMouseButton.new()
	ev.button_index = MOUSE_BUTTON_LEFT
	ev.pressed = pressed
	ev.position = pos
	ev.global_position = pos
	root.push_input(ev)


func key(code: Key) -> void:
	var ev := InputEventKey.new()
	ev.keycode = code
	ev.pressed = true
	root.push_input(ev)


func _initialize() -> void:
	var out := String(U.parse_args()["out"])
	SaveStore.dir = "user://interactive_saves"
	DirAccess.make_dir_recursive_absolute(out)
	Session.choose_scenario("direct_play")
	main = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)
	await process_frame
	await process_frame
	var bed: Control = main.bed
	var runner: Node = main.runner

	# 영역 끌기 (칸 크기 44): 왼쪽 위 칸에서 (행 1, 열 3) 칸까지
	mouse(bed.global_position + Vector2(5, 5), true)
	mouse(bed.global_position + Vector2(44 * 3 + 5, 44 + 5), false)
	await process_frame
	print("drag_select %s (expect [0, 0, 2, 4])" % [bed.selection])
	mouse(bed.global_position + Vector2(44 * 7 + 5, 44 * 2 + 5), true)
	mouse(bed.global_position + Vector2(44 * 7 + 5, 44 * 2 + 5), false)
	print("click_select %s (expect [2, 7, 1, 1])" % [bed.selection])

	# 키
	for k in [KEY_3, KEY_SPACE, KEY_SPACE, KEY_4, KEY_SPACE, KEY_1]:
		key(k)
		await process_frame
		print("key %s -> speed_index %d" % [OS.get_keycode_string(k), runner.speed_index])
	key(KEY_SPACE)
	await process_frame
	print("paused: speed_index %d" % runner.speed_index)

	# 자동 물 주기 입력칸 (바꾸면 바로 러너 설정이 바뀜)
	main._auto_hour.value = 12
	main._auto_mm.value = 6.0
	main._auto_check.button_pressed = false
	print("auto settings now %s (expect enabled=false hour=12 mm=6.0)" % [runner.auto_irrigation])
	main._auto_check.button_pressed = true

	# 정식 (행동 패널 단추), 저장·불러오기 키
	main.action_panel.select_action("plant")
	main.action_panel._run.pressed.emit()
	await process_frame
	print("pending after plant button: %d" % runner.world.get_pending_command_count())
	runner.set_speed(0)
	runner.step(24 * 3)
	key(KEY_F5)
	await process_frame
	var saved_tick: int = runner.world.get_tick()
	var saved_hash: String = runner.world.state_hash()
	runner.step(48)
	key(KEY_F9)
	await process_frame
	print("F5 at tick %d, F9 -> tick %d hash_equal=%s speed_index=%d" % [saved_tick, runner.world.get_tick(), runner.world.state_hash() == saved_hash, runner.speed_index])
	print("auto settings after F9 %s" % [runner.auto_irrigation])

	# F3 개발자 패널 끄기·켜기
	key(KEY_F3)
	await process_frame
	var hidden: bool = not main.dev.visible
	key(KEY_F3)
	await process_frame
	print("F3 toggled: hidden=%s then visible=%s" % [hidden, main.dev.visible])

	# 가장 빠른 배속으로 1작기 끝까지 실제 프레임으로 (watch 판 새로 시작)
	main.free()
	Session.choose_scenario("season_standard")
	main = load("res://scenes/main.tscn").instantiate()
	root.add_child(main)
	await process_frame
	runner = main.runner
	runner.autosave_enabled = false
	key(KEY_4)
	var t0 := Time.get_ticks_msec()
	var frames := 0
	var sum_ms := 0.0
	var max_ms := 0.0
	var last := Time.get_ticks_usec()
	while not main.end_panel.visible and Time.get_ticks_msec() - t0 < 120000:
		await process_frame
		var now := Time.get_ticks_usec()
		var ms := float(now - last) / 1000.0
		last = now
		frames += 1
		sum_ms += ms
		max_ms = maxf(max_ms, ms)
	print("season run at speed 4: end_panel=%s tick=%d real_seconds=%.1f frames=%d mean_frame_ms=%.2f max_frame_ms=%.2f capped_frames=%d" % [
		main.end_panel.visible, runner.world.get_tick(), float(Time.get_ticks_msec() - t0) / 1000.0, frames, sum_ms / maxf(1.0, frames), max_ms, runner.stats["capped_frames"]])
	print("end label: %s" % main._end_label.text.replace("\n", " | "))
	main.free()
	await process_frame
	quit(0)
