# C10 (m6b-character-play 9.1, 2.3·R12): 칸 정보. 꽃·열매가 있는 칸과 빈 칸을 바라봐요 → 개수가 스냅샷(get_tile_visuals)과 같고
# 문구가 "꽃·꽃봉오리 N개"(코어의 open_flower_count는 안 핀 꽃봉오리를 포함해요). 보이는 것만 써요(진짜 값 접근자 없음).
#  1) 글 함수: 심은 칸, 빈 칸, 죽은 포기, 대상 없음, 밀도 모름
#  2) 실제 판(처방대로 키운 작기 중간): 글이 보이는 값과 같고, 밀도는 시나리오 [crop] / 정식 명령에서
#  3) 그림(딸기 단계 모델이 없을 때의 예전 자리로 강제): 칸마다 그린 꽃·열매 수가 개수와 같은 방향(0이면 0, 많으면 많이, 자리 수에서 잘림), MultiMesh 합과 같음
#  4) 캐릭터 화면 칩: 대상 칸을 바꾸면 글이 바뀌고, 대상 없음이면 숨김
#   godot --headless --path game --script res://tests/c10_cell_info.gd -- out=<폴더>
extends SceneTree

const U = preload("res://tests/util.gd")
const Driver = preload("res://tests/char_driver.gd")
const CellInfo = preload("res://scripts/cell_info.gd")
const HouseView3D = preload("res://scripts/house_view_3d.gd")
const SaveStore = preload("res://scripts/save_store.gd")
const Session = preload("res://scripts/session.gd")
const Lib = preload("res://scripts/art_library.gd")

var fails := PackedStringArray()


func _ok(name: String, cond: bool, detail := "") -> void:
	if not cond:
		fails.push_back("%s %s" % [name, detail])
	print("%s %s %s" % ["ok  " if cond else "FAIL", name, detail])


func _v(present: int, alive: int, fl: int, fr: int, rp: int) -> Dictionary:
	return {"crop_present": PackedByteArray([present]), "crop_alive": PackedByteArray([alive]),
		"open_flower_count": PackedInt32Array([fl]), "fruit_count": PackedInt32Array([fr]), "ripening_fruit_count": PackedInt32Array([rp])}


func _initialize() -> void:
	var args := U.parse_args()
	SaveStore.dir = String(args["out"]).path_join("c10_saves")
	# ---- 1) 글 ----
	var t1 := CellInfo.text(_v(1, 1, 24, 16, 4), 0, 8.0)
	print("chip_planted=%s" % t1.replace("\n", " / "))
	_ok("planted_text", t1.contains("이 칸 포기 8개 모두") and t1.contains("꽃·꽃봉오리 24개") and t1.contains("열매 16개") and t1.contains("익어 가는 것 4개") and t1.contains("포기당 꽃·꽃봉오리 약 3개") and t1.contains("열매 약 2개"))
	_ok("per_plant_decimal", CellInfo.text(_v(1, 1, 19, 0, 0), 0, 8.0).contains("약 2.4개"))
	_ok("empty_cell", CellInfo.text(_v(0, 0, 0, 0, 0), 0, 8.0) == "빈 칸")
	_ok("dead_plant", CellInfo.text(_v(1, 0, 3, 0, 0), 0, 8.0) == "죽은 포기")
	_ok("no_target_hidden", CellInfo.text(_v(1, 1, 1, 1, 0), -1, 8.0) == "")
	var t_unknown := CellInfo.text(_v(1, 1, 5, 2, 1), 0, -1.0)
	_ok("unknown_density_no_per_plant_line", t_unknown.contains("이 칸 포기 전체") and not t_unknown.contains("포기당"), t_unknown)
	# ---- 2) 실제 판 (처방 일정표대로 키운 작기 중간) ----
	var s: Object = Driver.new_session("season_standard")
	_ok("density_from_scenario_crop", is_equal_approx(s.plant_density(), 8.0), str(s.plant_density()))
	s.step_ticks(24 * 80)
	var v: Dictionary = s.world().get_tile_visuals(0.8)
	var pick := -1
	for i in range(80):
		if v["crop_alive"][i] == 1 and v["open_flower_count"][i] > 0 and v["fruit_count"][i] > 0 and pick < 0:
			pick = i
	_ok("found_cell_with_flowers_and_fruit", pick >= 0)
	if pick >= 0:
		var tt := CellInfo.text(v, pick, s.plant_density())
		print("chip_real cell=%d %s" % [pick, tt.replace("\n", " / ")])
		_ok("real_text_matches_visuals", tt.contains("꽃·꽃봉오리 %d개" % v["open_flower_count"][pick]) and tt.contains("열매 %d개" % v["fruit_count"][pick]) and tt.contains("익어 가는 것 %d개" % v["ripening_fruit_count"][pick]))
	# ---- 3) 그림 덧붙임 (예전 자리: 딸기 단계 모델 파일이 없을 때의 그림. 단계 모델은 꽃·열매가 모델 안에 있어 C13 D가 봐요) ----
	Lib.model_dir = "res://art/c10_no_models"
	Lib.clear_cache()
	var view: Node3D = HouseView3D.new()
	get_root().add_child(view)
	view.setup(s)
	view.refresh_visuals()
	var sum := {"flower": 0, "fruit_green": 0, "fruit_red": 0}
	var dir_ok := true
	var cap_ok := true
	for i in range(80):
		var dr: Dictionary = view.drawn_addons_for_cell(i)
		for k in sum:
			sum[k] += dr[k]
		var fl: int = v["open_flower_count"][i]
		var gr: int = v["fruit_count"][i] - v["ripening_fruit_count"][i]
		var rp: int = v["ripening_fruit_count"][i]
		if v["crop_alive"][i] == 1:
			if (fl > 0) != (dr["flower"] > 0) or (gr > 0) != (dr["fruit_green"] > 0) or (rp > 0) != (dr["fruit_red"] > 0):
				dir_ok = false
			if dr["flower"] > fl or dr["fruit_green"] > gr or dr["fruit_red"] > rp:
				cap_ok = false
	_ok("drawn_zero_iff_count_zero", dir_ok)
	_ok("drawn_never_more_than_count", cap_ok)
	var mm: Dictionary = view.plant_instance_counts()
	_ok("multimesh_totals_match_per_cell_sum", mm["flower"] == sum["flower"] and mm["fruit_green"] == sum["fruit_green"] and mm["fruit_red"] == sum["fruit_red"], "%s vs %s" % [mm, sum])
	if pick >= 0:
		var mono := true
		var prev := -1
		for n in [0, 4, 8, 16, 24, 40, 80]:
			var vv: Dictionary = v.duplicate(true)
			vv["open_flower_count"][pick] = n
			view._last_v = vv
			var dn: int = view.drawn_addons_for_cell(pick)["flower"]
			if dn < prev:
				mono = false
			prev = dn
		_ok("more_flowers_more_drawn_capped", mono and prev == 8 * 4, "last=%d" % prev)
	_ok("fallback_view_without_stage_models", not view.stage_mode)
	view.free()
	Lib.model_dir = Lib.MODEL_DIR
	Lib.clear_cache()
	s.dispose()
	# 빈 밭: 시나리오에 작물이 없으면 밀도 모름 → 정식하면 그 명령의 밀도
	var e: Object = Driver.new_session("direct_play", false)
	var d0: float = e.plant_density()
	var de: Object = Driver.new(e)
	de.open_device("bench_plant")
	e.device_command({"action": "plant", "seedling_grade": "B", "plants_per_m2": 6.0})
	e.step_ticks(1)
	_ok("density_unknown_then_from_plant_command", d0 < 0.0 and is_equal_approx(e.plant_density(), 6.0), "%.1f -> %.1f" % [d0, e.plant_density()])
	e.dispose()
	# ---- 4) 캐릭터 화면 칩 (처방대로 키운 밭) ----
	Session.choose_scenario("season_standard")
	var cp: Node = load("res://scenes/char_play.tscn").instantiate()
	get_root().add_child(cp)
	for i in range(3):
		await process_frame
	var cs: Object = cp.s
	cs.step_ticks(24 * 80)
	cp._refresh(true)
	var target := pick if pick >= 0 else 45
	var sp: Array = cs.layout.stand_point_for_cell(target)
	cs.place(sp[0], sp[1])
	cp.update_cell_chip()
	var want := CellInfo.text(cp.view._last_v, target, cs.plant_density())
	_ok("chip_shows_target_cell", cp._cell_chip.visible and cp._cell_label.text == want and want.contains("꽃·꽃봉오리"), want.replace("\n", " / "))
	cs.place(Vector2(cs.layout.aisle_center_x(1), -1.0), Vector2i(0, -1))
	cp.update_cell_chip()
	_ok("chip_hidden_without_target", not cp._cell_chip.visible)
	cp.queue_free()
	await process_frame
	Session.scenario_id = ""
	U.finish(self, fails)
