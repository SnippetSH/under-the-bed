# C8 (m6b-character-play 9.1, 6장): 행동 표시와 취소. 행동은 화면 쪽이 들고 있다가 다음 1시간 직전에 코어에 넣어요(R11).
#  1) 붙잡기·Z(하나)·Shift+Z(한 번 누르고 걸은 구간)·넣는 순서·표시, 시간이 시작된 뒤에는 취소할 것 없음
#  2) 취소가 섞인 판 A와 취소한 행동을 처음부터 안 한 판 B: 모든 틱 해시·명령 기록이 같음(엔진→엔진), A의 기록 → sim-cli도 같음.
#     자동 물 주기는 취소 대상이 아니라 두 판 모두 기록에 남아요
#  3) 정식 미리 보기(R18): 정식 직후 640포기가 그려지고, Z로 사라지고, 코어에는 영향 없음. 넣을 때 거부되면 사라지고 한국어 알림.
#     기본 등급 B는 다음 시간의 진짜 그림과 같아서 튀지 않음
#  4) 일하는 속도 10배에서도 A의 최종 해시가 같음
#   godot --headless --path game --script res://tests/c08_cancel.gd -- out=<폴더> cli=<sim-cli>
extends SceneTree

const U = preload("res://tests/util.gd")
const Driver = preload("res://tests/char_driver.gd")
const Cfg = preload("res://scripts/cfg.gd")
const SaveStore = preload("res://scripts/save_store.gd")
const HouseView3D = preload("res://scripts/house_view_3d.gd")
const CellInfo = preload("res://scripts/cell_info.gd")
const Art = preload("res://scripts/plant_art.gd")

const SCENARIO_ID := "direct_play"

var fails := PackedStringArray()


func _ok(name: String, cond: bool, detail := "") -> void:
	if not cond:
		fails.push_back("%s %s" % [name, detail])
	print("%s %s %s" % ["ok  " if cond else "FAIL", name, detail])


func _held_cells(s: Object) -> Array:
	return s.held.map(func(e): return e["cell"])


func _on_tick(s: Object, hashes: PackedStringArray) -> void:
	hashes.push_back(s.world().state_hash())


# 판 A(취소 있음) / B(취소한 행동을 처음부터 안 함). 둘 다 같은 틱 흐름. 자동 물 주기 켜짐(8시)
func _run(s: Object, with_cancels: bool, hashes: PackedStringArray) -> void:
	var d: Object = Driver.new(s)
	s.tick_hook = _on_tick.bind(s, hashes)
	var L: Object = s.layout
	# 틱 0: 퇴비 0행 전체(한 묶음)
	d.hold_walk(0, 1, 2, -0.6, L.length_m + 0.6)
	if with_cancels:
		d.hold_walk(1, -1, 4, -0.6, L.length_m + 0.6)    # 괭이 1행 전체 → Shift+Z로 통째 취소
		s.cancel_last_stroke()
	d.use_tool(21, 3)                                      # 석회
	if with_cancels:
		d.use_tool(5, 1)                                    # 비료 → Z
		s.cancel_last()
	d.open_device("heater")
	s.device_command({"action": "set_heater", "on": true, "setpoint_degC": 8.0})
	if with_cancels:
		d.open_device("fan")
		s.device_command({"action": "set_fan", "on": true})  # → Z
		s.cancel_last()
	d.wait_ticks(1)
	d.open_device("bench_plant")
	s.device_command({"action": "plant", "seedling_grade": "B", "plants_per_m2": 8.0})
	if with_cancels:
		s.cancel_last()                                     # 정식 → Z, 다시 정식
		s.device_command({"action": "plant", "seedling_grade": "B", "plants_per_m2": 8.0})
	d.wait_ticks(2)
	# 물뿌리개 2행: A는 끝까지 걷고 Z 세 번(마지막 세 칸 취소), B는 앞 17칸만
	if with_cancels:
		d.hold_walk(1, 1, 0, -0.6, L.length_m + 0.6)
		for i in range(3):
			s.cancel_last()
	else:
		d.hold_walk(1, 1, 0, -0.6, 16.9)
	d.wait_ticks(1)
	d.open_device("door")
	s.go_to_sleep()
	d.fast_forward()
	d.wait_ticks(4)
	s.tick_hook = Callable()


func _initialize() -> void:
	var args := U.parse_args()
	SaveStore.dir = String(args["out"]).path_join("c08_saves")
	# ---- 1) 논리 ----
	var s: Object = Driver.new_session(SCENARIO_ID)
	var d: Object = Driver.new(s)
	var log0: int = s.world().get_command_log().size()
	d.use_tool(3, 0)
	d.use_tool(4, 0)
	d.use_tool(4, 1)     # 같은 칸에 물뿌리개 다음 비료
	d.use_tool(5, 0)
	_ok("held_not_in_core_until_tick", s.held_count() == 4 and s.world().get_pending_command_count() == 0 and s.world().get_command_log().size() == log0)
	s.cancel_last()
	_ok("Z_removes_most_recent_and_its_marker", _held_cells(s) == [3, 4, 4] and not s.markers.has(5))
	s.cancel_last()
	_ok("marker_reflects_remaining_on_same_cell", s.markers.get(4, []) == [0], str(s.markers))
	var before: int = s.held_count()
	d.hold_walk(1, -1, 0, -0.6, s.layout.length_m + 0.6)
	var stroke_n: int = s.held_count() - before
	var removed: Array = s.cancel_last_stroke()
	_ok("shift_Z_removes_exactly_the_stroke", stroke_n == 20 and removed.size() == 20 and s.held_count() == before, "stroke=%d removed=%d" % [stroke_n, removed.size()])
	d.hold_walk(1, -1, 0, 9.5, 14.6)    # 9~14열 6칸
	s.cancel_last()
	s.cancel_last()
	var rest: Array = s.cancel_last_stroke()
	_ok("Z_after_partial_then_shift_Z_takes_rest_of_stroke", rest.size() == 4 and _held_cells(s) == [3, 4], "rest=%d held=%s" % [rest.size(), _held_cells(s)])
	s.cancel_last()
	s.cancel_last()
	_ok("empty_cancel_returns_nothing", s.cancel_last().is_empty() and s.cancel_last_stroke().is_empty() and s.markers.is_empty())
	d.use_tool(7, 0)
	d.use_tool(8, 3)
	var t0: int = s.world().get_tick()
	d.wait_ticks(1)
	var newlog: Array = s.world().get_command_log().slice(log0)
	var tool_log := newlog.filter(func(e): return int(e["command"]["area"][2]) == 1)
	var cells := tool_log.map(func(e): return int(e["command"]["area"][1]))
	_ok("flush_in_action_order_before_tick", tool_log.size() == 2 and cells == [7, 8] and int(tool_log[0]["tick"]) == t0 and s.held_count() == 0 and s.markers.is_empty(), str(cells))
	_ok("nothing_to_cancel_after_hour_started", s.cancel_last().is_empty())
	s.dispose()

	# ---- 2) 취소 있는 판 A = 취소한 행동이 없던 판 B ----
	var ha := PackedStringArray()
	var hb := PackedStringArray()
	var a: Object = Driver.new_session(SCENARIO_ID)
	ha.push_back(a.world().state_hash())
	_run(a, true, ha)
	var b: Object = Driver.new_session(SCENARIO_ID)
	hb.push_back(b.world().state_hash())
	_run(b, false, hb)
	var diff := U.compare(ha, hb)
	_ok("with_cancels_equals_never_made_all_ticks", diff == "" and ha.size() >= 9, "rows=%d %s final=%s" % [ha.size(), "MATCH" if diff == "" else diff, ha[-1]])
	var la: Array = a.world().get_command_log()
	var lb: Array = b.world().get_command_log()
	_ok("command_logs_identical", JSON.stringify(la) == JSON.stringify(lb), "a=%d b=%d" % [la.size(), lb.size()])
	var cancelled_in_log := la.filter(func(e): return e["command"]["action"] in ["till", "set_fan"] or (e["command"]["action"] == "add_fertilizer"))
	_ok("cancelled_actions_not_in_log", cancelled_in_log.is_empty(), str(cancelled_in_log.size()))
	var g: Vector2i = a.world().get_grid_size()
	var auto := la.filter(func(e): return e["command"]["action"] == "irrigate" and Array(e["command"]["area"]).map(func(v): return int(v)) == [0, 0, g.y, g.x])
	_ok("auto_irrigation_stays_in_log", auto.size() >= 1 and int(auto[0]["tick"]) % 24 == int(a.runner.auto_irrigation["hour"]), "auto=%d" % auto.size())
	var replay := U.write_replay(args["out"], "c08_replay.toml", Cfg.scenario(SCENARIO_ID)["path"], la)
	if args["cli"] == "":
		print("engine_to_cli SKIPPED (no cli=)")
	else:
		var ch := U.run_cli(args["cli"], args["params"], replay, a.world().get_tick(), ProjectSettings.globalize_path(String(args["out"]).path_join("c08_replay_cli.csv")))
		var d2 := U.compare(ha, ch)
		_ok("with_cancels_engine_to_cli_all_ticks", d2 == "" and ch.size() == ha.size(), "rows=%d %s" % [ch.size(), "MATCH" if d2 == "" else d2])

	# ---- 4) 속도 10배 ----
	var hf := PackedStringArray()
	var f: Object = Driver.new_session(SCENARIO_ID)
	f.base_seconds_per_hour = float(Cfg.get_v("char_time", "seconds_per_game_hour")) / 10.0
	hf.push_back(f.world().state_hash())
	_run(f, true, hf)
	_ok("speed_x10_same_final_hash", f.world().state_hash() == a.world().state_hash())
	a.dispose()
	b.dispose()
	f.dispose()

	# ---- 3) 정식 미리 보기 ----
	var p: Object = Driver.new_session(SCENARIO_ID, false)
	var view: Node3D = HouseView3D.new()
	get_root().add_child(view)
	await process_frame
	view.setup(p)
	view.refresh_visuals()
	_ok("empty_field_no_plants", view.plant_instance_counts()[view.seedling_slot()] == 0)
	var notes: Array = []
	p.notice.connect(func(t: String): notes.append(t))
	p.device_command({"action": "plant", "seedling_grade": "B", "plants_per_m2": 8.0})
	view.refresh_visuals()
	var c1: Dictionary = view.plant_instance_counts()
	_ok("preview_draws_seedlings_on_every_cell", p.has_held_plant() and view.preview_plant and c1[view.seedling_slot()] == 640, str(c1))
	_ok("preview_chip_text", CellInfo.text(view._last_v, 5, p.plant_density(), view.preview_plant) == "방금 심은 모종 (다음 시간에 자리 잡아요)")
	var h0: String = p.world().state_hash()
	p.cancel_last()
	view.refresh_visuals()
	_ok("Z_removes_preview", not p.has_held_plant() and view.plant_instance_counts()[view.seedling_slot()] == 0)
	_ok("preview_has_no_effect_on_core", p.world().state_hash() == h0 and p.world().get_pending_command_count() == 0)
	var keys := {}
	for grade in ["A", "B", "C"]:
		var sg: Object = Driver.new_session(SCENARIO_ID, false)
		sg.device_command({"action": "plant", "seedling_grade": grade, "plants_per_m2": 8.0})
		sg.step_ticks(1)
		keys[grade] = Art.key_for(sg.world().get_tile_visuals(float(Cfg.get_v("visual", "ripening_min_index"))), 0)
		sg.dispose()
	print("real key one hour after planting by grade: %s (preview draws p0000)" % [keys])
	_ok("no_pop_for_default_grade_B", keys["B"] == "p0000")
	# 넣을 때 거부되면 미리 보기가 사라지고 한국어 알림 (밀도 0은 can_enqueue를 지나지 않으므로 직접 붙잡아 둬요)
	p.held.append({"cmd": {"action": "plant", "seedling_grade": "B", "plants_per_m2": 0.0}, "cell": -1, "tool": -1, "stroke": 99})
	view.refresh_visuals()
	var had: bool = view.preview_plant
	notes.clear()
	p.step_ticks(1)
	view.refresh_visuals()
	var ascii := RegEx.create_from_string("[A-Za-z_]")
	_ok("flush_rejection_removes_preview_with_korean_notice", had and not view.preview_plant and view.plant_instance_counts()[view.seedling_slot()] == 0 and notes.size() >= 1 and ascii.search(str(notes[0])) == null, str(notes))
	view.free()
	p.dispose()
	U.finish(self, fails)
