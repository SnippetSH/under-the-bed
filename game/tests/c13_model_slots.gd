# C13 (m6b-character-play 9.1, 2.2): 모델 자리. 모델 파일이 있을 때와 없을 때, 캐릭터 모델과 동작 이름.
#  A) 그림 자리(art_library): 파일이 없으면 코드로 만든 모델, .obj(+.mtl)·.glb를 넣으면 그 모델, 둘 다 있으면 .glb,
#     캐릭터·도구 PNG 자리, 지운 뒤 다시 코드 모델. 시험 파일은 직접 만들고(내려받기 없음) 끝나면 지워요.
#  B) 캐릭터 리그(character_rig): 뼈대 없는 모델 + 노드 애니메이션 "walk"만, 뼈대·스킨 모델 + "idle"·"walk"·"work"
#     (시험 .glb는 user://에 써요. 저장소의 character.glb는 건드리지 않아요)
#  C) 저장소의 character.glb(디렉터 모델): 읽히고 동작 idle·walk·work가 있음, 화면이 그 모델을 써요(평면 그림 숨김).
#     자리에 파일이 없으면 화면은 평면 그림으로 돌아가요
#  D) 디렉터 딸기 생육 단계 모델 8개: 해석, 단계 고르기, 실제 판, 늦봄 런너 모습(꾸밈), 파일이 없을 때 예전 자리 (아래 part_d 머리 주석)
#   godot --headless --path game --script res://tests/c13_model_slots.gd -- out=<폴더>
extends SceneTree

const U = preload("res://tests/util.gd")
const Lib = preload("res://scripts/art_library.gd")
const Rig = preload("res://scripts/character_rig.gd")
const Driver = preload("res://tests/char_driver.gd")
const HouseView3D = preload("res://scripts/house_view_3d.gd")
const SaveStore = preload("res://scripts/save_store.gd")
const TEST_PATH := "user://c13_test_character.glb"

var fails := PackedStringArray()
var made: Array = []


func _ok(name: String, cond: bool, detail := "") -> void:
	if not cond:
		fails.push_back("%s %s" % [name, detail])
	print("%s %s %s" % ["ok  " if cond else "FAIL", name, detail])


func _write(path: String, text: String) -> void:
	var f := FileAccess.open(path, FileAccess.WRITE)
	f.store_string(text)
	f.close()
	made.append(path)


func _write_glb(path: String, mesh: Mesh) -> int:
	var root := Node3D.new()
	var mi := MeshInstance3D.new()
	mi.mesh = mesh
	mi.position = Vector3(0, 0.5, 0)
	root.add_child(mi)
	var doc := GLTFDocument.new()
	var st := GLTFState.new()
	var e := doc.append_from_scene(root, st)
	if e == OK:
		e = doc.write_to_filesystem(st, ProjectSettings.globalize_path(path))
	root.free()
	made.append(path)
	return e


func _box_mesh(size := Vector3.ONE) -> ArrayMesh:
	var bm := BoxMesh.new()
	bm.size = size
	var am := ArrayMesh.new()
	am.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, bm.get_mesh_arrays())
	var m := StandardMaterial3D.new()
	m.albedo_color = Color(0.2, 0.4, 0.9)
	am.surface_set_material(0, m)
	return am


func _export(root: Node3D, path: String) -> int:
	get_root().add_child(root)
	var doc := GLTFDocument.new()
	var st := GLTFState.new()
	var e := doc.append_from_scene(root, st)
	if e == OK:
		e = doc.write_to_filesystem(st, ProjectSettings.globalize_path(path))
	get_root().remove_child(root)
	root.free()
	return e


# 뼈대 없음, 몸통 노드가 위아래로 움직이는 "walk"
func _make_unskinned() -> Node3D:
	var root := Node3D.new()
	root.name = "Character"
	var body := MeshInstance3D.new()
	body.name = "Body"
	body.mesh = _box_mesh(Vector3(0.4, 1.0, 0.25))
	body.position = Vector3(0, 0.5, 0)
	root.add_child(body)
	body.owner = root
	var ap := AnimationPlayer.new()
	ap.name = "AnimationPlayer"
	root.add_child(ap)
	ap.owner = root
	var a := Animation.new()
	a.length = 0.5
	var t := a.add_track(Animation.TYPE_POSITION_3D)
	a.track_set_path(t, "Body")
	a.position_track_insert_key(t, 0.0, Vector3(0, 0.5, 0))
	a.position_track_insert_key(t, 0.25, Vector3(0, 0.6, 0))
	a.position_track_insert_key(t, 0.5, Vector3(0, 0.5, 0))
	var lib := AnimationLibrary.new()
	lib.add_animation("walk", a)
	ap.add_animation_library("", lib)
	return root


# 2뼈 뼈대 + 모든 정점이 뼈 1(upper)에 붙은 상자 + 뼈 회전 애니메이션 3개
func _make_skinned() -> Node3D:
	var root := Node3D.new()
	root.name = "Character"
	var sk := Skeleton3D.new()
	sk.name = "Skeleton"
	root.add_child(sk)
	sk.owner = root
	sk.add_bone("hips")
	sk.add_bone("upper")
	sk.set_bone_parent(1, 0)
	sk.set_bone_rest(0, Transform3D(Basis.IDENTITY, Vector3(0, 0.5, 0)))
	sk.set_bone_rest(1, Transform3D(Basis.IDENTITY, Vector3(0, 0.3, 0)))
	sk.reset_bone_poses()
	var arr := BoxMesh.new().get_mesh_arrays()
	var nv: int = (arr[Mesh.ARRAY_VERTEX] as PackedVector3Array).size()
	var bones := PackedInt32Array()
	var weights := PackedFloat32Array()
	for i in range(nv):
		bones.append_array([1, 0, 0, 0])
		weights.append_array([1.0, 0.0, 0.0, 0.0])
	arr[Mesh.ARRAY_BONES] = bones
	arr[Mesh.ARRAY_WEIGHTS] = weights
	var am := ArrayMesh.new()
	am.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arr)
	var mi := MeshInstance3D.new()
	mi.name = "BodyMesh"
	mi.mesh = am
	sk.add_child(mi)
	mi.owner = root
	mi.skeleton = NodePath("..")
	mi.skin = sk.create_skin_from_rest_transforms()
	var ap := AnimationPlayer.new()
	ap.name = "AnimationPlayer"
	root.add_child(ap)
	ap.owner = root
	var lib := AnimationLibrary.new()
	var angles := {"idle": 0.1, "walk": 0.6, "work": 1.2}
	for n in angles:
		var a := Animation.new()
		a.length = 0.5
		var t := a.add_track(Animation.TYPE_ROTATION_3D)
		a.track_set_path(t, "Skeleton:upper")
		a.rotation_track_insert_key(t, 0.0, Quaternion.IDENTITY)
		a.rotation_track_insert_key(t, 0.25, Quaternion(Vector3.FORWARD, angles[n]))
		a.rotation_track_insert_key(t, 0.5, Quaternion.IDENTITY)
		lib.add_animation(n, a)
	ap.add_animation_library("", lib)
	return root


func _find(n: Node, cls: String) -> Node:
	if n.is_class(cls):
		return n
	for c in n.get_children():
		var f := _find(c, cls)
		if f != null:
			return f
	return null


func _initialize() -> void:
	var args := U.parse_args()
	SaveStore.dir = String(args["out"]).path_join("c13_saves")
	part_a()
	part_b()
	await part_c()
	await part_d()
	U.finish(self, fails)


# ---------- D) 디렉터 딸기 생육 단계 모델 (2026-11-20) ----------
#  D1 해석기: 8개 파일(o 여러 개, usemtl 여러 묶음, 함께 쓰는 mtllib, 법선만·UV 없음) → 삼각형 수 = 파일 머리의 faces,
#     표면 수 = 재질 이름 수, 재질 색 = .mtl Kd, 법선 있음, 크기·땅 아래로 내려간 부분
#  D2 단계 고르기(보이는 것만): 죽음 s7, 몫에 익어 가는 열매 s5, 열매 s4, 꽃 s3, 잎 크기 1단계 s1·그 위 s2.
#     꽃·열매 모습의 포기 수가 칸의 개수를 넘지 않음 (꽃 2개 칸 = 꽃 핀 포기 2개)
#  D3 실제 판(처방대로 80일): 화면이 단계 모델을 쓰고 덧붙임 없음, 칸마다 단계 수 = 규칙, MultiMesh 합 = 그린 포기 수
#  D4 런너 모습(꾸밈): 시작일 전 0, N단계 뒤 바뀐 몫 ≈ 1 − (1 − f)^N, 열매 몫 있으면 열매 런너·없으면(꽃만 포함) 열매 없는 런너,
#     하나만 있으면 그것으로, 죽음은 s7. 다시 계산·저장 후 불러오기에도 같음. 해시·칸 정보에 영향 없음
#  D5 파일이 없으면 예전 자리(크기 4단계 + 덧붙임)
const Art = preload("res://scripts/plant_art.gd")
const Cfg = preload("res://scripts/cfg.gd")
const CellInfo = preload("res://scripts/cell_info.gd")


func _fake_v(cells: Array) -> Dictionary:
	# cells: [[present, alive, leaf_g, flowers, fruit, ripening] ...]
	var v := {"crop_present": PackedInt32Array(), "crop_alive": PackedInt32Array(), "leaf_g_per_m2": PackedFloat32Array(),
			"open_flower_count": PackedInt32Array(), "fruit_count": PackedInt32Array(), "ripening_fruit_count": PackedInt32Array()}
	for c in cells:
		v["crop_present"].append(c[0])
		v["crop_alive"].append(c[1])
		v["leaf_g_per_m2"].append(c[2])
		v["open_flower_count"].append(c[3])
		v["fruit_count"].append(c[4])
		v["ripening_fruit_count"].append(c[5])
	return v


func _stages(v: Dictionary, i: int, steps: int, hb := true, hn := true) -> Array:
	var out := []
	for k in range(8):
		out.append(Art.stage_for_plant(v, i, k, 8, steps, hb, hn))
	return out


func _count(a: Array, st: int) -> int:
	return a.filter(func(x): return x == st).size()


# 달력: 작기 시작 doy0(0부터) + d일 h시의 get_time() 모양
func _t(start_doy0: int, d: int, h := 6) -> Dictionary:
	return {"elapsed_days": d, "day_of_year": posmod(start_doy0 + d, 365) + 1, "hour": h}


func part_d() -> void:
	var per := 8
	# D1
	var want_kd := {"fruit_ripe": Color(0.86, 0.06, 0.09), "leaf_dark": Color(0.13, 0.42, 0.14), "runner_red": Color(0.66, 0.4, 0.3)}
	for slot in Lib.STAGE_SLOTS:
		var path := Lib.model_path(slot)
		var text := FileAccess.get_file_as_string(path) if path != "" else ""
		var faces := -1
		var re := RegEx.create_from_string("faces=(\\d+)")
		var mm := re.search(text)
		if mm:
			faces = int(mm.get_string(1))
		var names := {}
		for line in text.split("\n"):
			if line.begins_with("usemtl "):
				names[line.substr(7).strip_edges()] = true
		var m := Lib.model(slot)
		var ok: bool = not m.is_empty() and m["placeholder"] == false and path.ends_with(".obj")
		var mesh: Mesh = m.get("mesh") if ok else null
		var kd_ok := true
		var normals_ok := true
		if mesh != null:
			for si in range(mesh.get_surface_count()):
				var arr := mesh.surface_get_arrays(si)
				if arr[Mesh.ARRAY_NORMAL] == null or (arr[Mesh.ARRAY_NORMAL] as PackedVector3Array).size() != (arr[Mesh.ARRAY_VERTEX] as PackedVector3Array).size():
					normals_ok = false
			# 재질 이름은 표면 순서 = 파일에서 처음 나온 순서
			var order := []
			for line in text.split("\n"):
				if line.begins_with("usemtl "):
					var nm := line.substr(7).strip_edges()
					if not order.has(nm):
						order.append(nm)
			for si in range(mini(order.size(), mesh.get_surface_count())):
				if want_kd.has(order[si]) and not (mesh.surface_get_material(si) as StandardMaterial3D).albedo_color.is_equal_approx(want_kd[order[si]]):
					kd_ok = false
		var aabb: AABB = mesh.get_aabb() if mesh else AABB()
		_ok("D1_%s_parsed" % slot, ok and m["tris"] == faces and mesh.get_surface_count() == names.size() and kd_ok and normals_ok,
				"tris=%s faces=%d surfaces=%s materials=%d aabb=%s" % [m.get("tris", -1), faces, mesh.get_surface_count() if mesh else -1, names.size(), aabb])
	_ok("D1_has_stage_models", Lib.has_stage_models())
	# D2 단계 고르기 (런너 단계 0)
	var thr: Array = Cfg.get_v("visual", "leaf_stage_thresholds_g_per_m2")
	var small := float(thr[0]) * 0.5
	var big := float(thr[0]) * 2.0
	var v := _fake_v([[1, 1, small, 0, 0, 0], [1, 1, big, 0, 0, 0], [1, 1, big, 2, 0, 0], [1, 1, big, 10, 5, 3],
			[1, 0, big, 4, 4, 4], [0, 0, 0.0, 0, 0, 0], [1, 1, big, 40, 30, 12]])
	_ok("D2_small_leaf_s1", _stages(v, 0, 0) == [0, 0, 0, 0, 0, 0, 0, 0], str(_stages(v, 0, 0)))
	_ok("D2_bigger_leaf_s2", _count(_stages(v, 1, 0), Art.STAGE_S2) == 8)
	var c2 := _stages(v, 2, 0)
	_ok("D2_two_flowers_two_flowering_plants", _count(c2, Art.STAGE_S3) == 2 and _count(c2, Art.STAGE_S2) == 6, str(c2))
	var c3 := _stages(v, 3, 0)
	_ok("D2_ripe3_fruit5_flowers10", _count(c3, Art.STAGE_S5) == 3 and _count(c3, Art.STAGE_S4) == 2 and _count(c3, Art.STAGE_S3) == 3, str(c3))
	_ok("D2_dead_s7", _count(_stages(v, 4, 0), Art.STAGE_S7) == 8)
	_ok("D2_empty_none", _count(_stages(v, 5, 0), -1) == 8)
	_ok("D2_many_all_s5", _count(_stages(v, 6, 0), Art.STAGE_S5) == 8)
	# D3 실제 판 (처방대로 80일 = 꽃·열매가 있는 때)
	var s: Object = Driver.new_session("season_standard")
	s.step_ticks(24 * 80)
	var view: Node3D = HouseView3D.new()
	get_root().add_child(view)
	view.setup(s)
	view.refresh_visuals()
	var rv: Dictionary = view._last_v
	var rule_ok := true
	var drawn := 0
	var seen := {}
	for i in range(80):
		var st: Array = view.drawn_stages_for_cell(i)
		var rp := mini(per, int(rv["ripening_fruit_count"][i]))
		var fr := mini(per, int(rv["fruit_count"][i]))
		var fl := mini(per, int(rv["open_flower_count"][i]))
		if rv["crop_present"][i] == 1 and rv["crop_alive"][i] == 1:
			if _count(st, Art.STAGE_S5) != rp or _count(st, Art.STAGE_S4) != fr - rp or _count(st, Art.STAGE_S3) != maxi(0, fl - fr):
				rule_ok = false
		for x in st:
			if x >= 0:
				drawn += 1
				seen[x] = true
	var total := 0
	for k in view.plant_instance_counts():
		total += int(view.plant_instance_counts()[k])
	print("D3 day 80 drawn stages present=%s runner_steps=%d plants=%d mm=%s" % [seen.keys(), view.runner_steps_now, drawn, view.plant_instance_counts()])
	_ok("D3_screen_uses_stage_models_no_addons", view.stage_mode and not view.plant_instance_counts().has("flower") and view.drawn_addons_for_cell(0)["flower"] == 0)
	_ok("D3_stage_counts_follow_cell_counts", rule_ok)
	_ok("D3_multimesh_total_equals_drawn_plants", total == drawn and drawn == 640, "%d vs %d" % [total, drawn])
	view.free()
	s.dispose()
	# D4 런너 (달력만): 작기 시작 9월 10일(doy0 252)
	var start0 := 252
	var c := Art.runner_cfg()
	var start_doy0: int = Art.MONTH_START_DOY0[int(c["month"]) - 1] + int(c["day"]) - 1
	var to_start := posmod(start_doy0 - start0, 365)
	_ok("D4_no_steps_before_start_date", Art.runner_steps(_t(start0, to_start - 1, 23)) == 0 and Art.runner_steps(_t(start0, 10)) == 0)
	_ok("D4_first_step_on_start_date", Art.runner_steps(_t(start0, to_start, 0)) == 1)
	var f := float(c["fraction"])
	var step_d := float(c["step_days"])
	# 작기가 시작일 뒤에 시작하면(5월 1일 시작) 그 해의 시작일은 세지 않아요
	_ok("D4_season_starting_after_start_date_waits_a_year", Art.runner_steps(_t(120, 20)) == 0)
	var alive := []
	for i in range(80):
		alive.append([1, 1, big, 0, 0, 0])
	var va := _fake_v(alive)
	var frac_ok := true
	var detail := ""
	for n in [1, 5, 10, 20, 30]:
		var t := _t(start0, to_start + int(ceil((n - 1) * step_d)), 0)
		var steps := Art.runner_steps(t)
		var sw := 0
		for i in range(80):
			for k in range(per):
				if Art.stage_for_plant(va, i, k, per, steps) == Art.STAGE_S6_NO_BERRIES:
					sw += 1
		var expect := 1.0 - pow(1.0 - f, steps)
		var got := sw / 640.0
		detail += " N=%d got=%.3f expect=%.3f" % [steps, got, expect]
		if absf(got - expect) > 0.05:
			frac_ok = false
	print("D4 runner fraction over 640 plants:%s" % detail)
	_ok("D4_switched_fraction_close_to_geometric", frac_ok, detail)
	var late := 400
	var vb := _fake_v([[1, 1, big, 3, 0, 0], [1, 1, big, 0, 2, 0], [1, 1, big, 0, 2, 2], [1, 0, big, 0, 2, 2]])
	var b0 := _stages(vb, 0, late)
	var b1 := _stages(vb, 1, late)
	_ok("D4_flowers_only_no_berries_runner", _count(b0, Art.STAGE_S6_NO_BERRIES) == 8, str(b0))
	_ok("D4_fruit_share_berries_runner", _count(b1, Art.STAGE_S6_BERRIES) == 2 and _count(b1, Art.STAGE_S6_NO_BERRIES) == 6 and _count(_stages(vb, 2, late), Art.STAGE_S6_BERRIES) == 2, str(b1))
	_ok("D4_dead_never_runner", _count(_stages(vb, 3, late), Art.STAGE_S7) == 8)
	_ok("D4_missing_berries_file_uses_no_berries", _count(_stages(vb, 1, late, false, true), Art.STAGE_S6_NO_BERRIES) == 8)
	_ok("D4_missing_no_berries_file_uses_berries", _count(_stages(vb, 0, late, true, false), Art.STAGE_S6_BERRIES) == 8)
	_ok("D4_no_runner_files_no_runner_look", _count(_stages(vb, 0, late, false, false), Art.STAGE_S3) == 3)
	# 실제 판: 5월 중순 (심어 둔 밭, 9월 10일 + 247일) → 다시 계산·저장 후 불러오기에도 같음
	var r: Object = Driver.new_session("char_planted")
	r.step_ticks(24 * 247)
	var v1: Node3D = HouseView3D.new()
	get_root().add_child(v1)
	v1.setup(r)
	var h_before: String = r.world().state_hash()
	var info_before := CellInfo.text(r.world().get_tile_visuals(float(Cfg.get_v("visual", "ripening_min_index"))), 10, r.plant_density())
	v1.refresh_visuals()
	var a1 := []
	for i in range(80):
		a1.append_array(v1.drawn_stages_for_cell(i))
	v1.refresh_visuals()
	var a2 := []
	for i in range(80):
		a2.append_array(v1.drawn_stages_for_cell(i))
	var t_now: Dictionary = r.world().get_time()
	var runners := a1.filter(func(x): return x == Art.STAGE_S6_BERRIES or x == Art.STAGE_S6_NO_BERRIES).size()
	var living := a1.filter(func(x): return x >= 0 and x != Art.STAGE_S7).size()
	var dead_n := a1.filter(func(x): return x == Art.STAGE_S7).size()
	print("D4 mid-May %d/%d steps=%d runners=%d (berries %d) living=%d dead=%d expect_frac=%.3f got=%.3f" % [t_now["month"], t_now["day"], v1.runner_steps_now, runners,
			_count(a1, Art.STAGE_S6_BERRIES), living, dead_n, 1.0 - pow(1.0 - f, v1.runner_steps_now), runners / maxf(1.0, living)])
	_ok("D4_mid_may_has_some_runner_plants", runners > 0 and runners < living)
	_ok("D4_recomputed_same", a1 == a2)
	_ok("D4_decoration_only_hash_and_cell_info_unchanged", r.world().state_hash() == h_before
			and CellInfo.text(r.world().get_tile_visuals(float(Cfg.get_v("visual", "ripening_min_index"))), 10, r.plant_density()) == info_before)
	var sv: Dictionary = r.save_slot(SaveStore.QUICK)
	var r2: Object = Driver.new_session("direct_play")
	var lv: Dictionary = r2.load_slot(SaveStore.QUICK)
	var v2: Node3D = HouseView3D.new()
	get_root().add_child(v2)
	v2.setup(r2)
	v2.refresh_visuals()
	var a3 := []
	for i in range(80):
		a3.append_array(v2.drawn_stages_for_cell(i))
	_ok("D4_same_after_save_and_load", sv["ok"] and lv["ok"] and a3 == a1 and r2.world().state_hash() == h_before)
	v1.free()
	v2.free()
	r.dispose()
	r2.dispose()
	# D5 파일이 없을 때: 예전 자리 + 덧붙임
	Lib.model_dir = "res://art/c13_no_models"
	Lib.clear_cache()
	var s5: Object = Driver.new_session("season_standard")
	s5.step_ticks(24 * 80)
	var v5: Node3D = HouseView3D.new()
	get_root().add_child(v5)
	v5.setup(s5)
	v5.refresh_visuals()
	var cnt: Dictionary = v5.plant_instance_counts()
	_ok("D5_missing_stage_files_fall_back_to_old_slots_and_addons", not v5.stage_mode and not Lib.has_stage_models() and cnt.has("plant_stage4")
			and int(cnt["flower"]) > 0 and int(cnt["fruit_green"]) + int(cnt["fruit_red"]) > 0 and v5.seedling_slot() == "plant_stage1", str(cnt))
	v5.free()
	s5.dispose()
	Lib.model_dir = Lib.MODEL_DIR
	Lib.clear_cache()
	_ok("D5_back_to_stage_models", Lib.has_stage_models())


func part_a() -> void:
	for p in ["heater.obj", "heater.mtl", "heater.glb", "flower.glb", "flower.obj"]:
		if FileAccess.file_exists(Lib.MODEL_DIR.path_join(p)):
			fails.push_back("A: a test slot already has a file (%s), not touching it" % p)
			return
	for r in Lib.report():
		print("slot %-22s %s tris=%s" % [r["slot"], r["source"], r.get("tris", "-")])
	Lib.clear_cache()
	var h0 := Lib.model("heater")
	_ok("A1_missing_file_uses_placeholder", h0["placeholder"] == true, "tris=%d" % h0["tris"])
	var obj := "# 시험용 사면체\nmtllib heater.mtl\nv 0 0 0\nv 1 0 0\nv 0 0 1\nv 0 1 0\nusemtl red\nf 1 3 2\nf 1 2 4\nf 1 4 3\nf 2 3 4\n"
	_write(Lib.MODEL_DIR.path_join("heater.mtl"), "newmtl red\nKd 0.9 0.1 0.1\n")
	_write(Lib.MODEL_DIR.path_join("heater.obj"), obj)
	Lib.clear_cache()
	var h1 := Lib.model("heater")
	var col: Color = (h1["mesh"] as Mesh).surface_get_material(0).albedo_color
	_ok("A2_obj_override_picked_up", h1["placeholder"] == false and h1["tris"] == 4 and h1["path"].ends_with(".obj"), "path=%s tris=%d" % [h1["path"], h1["tris"]])
	_ok("A2_mtl_Kd_color", col.is_equal_approx(Color(0.9, 0.1, 0.1)), str(col))
	var aabb: AABB = (h1["mesh"] as Mesh).get_aabb()
	_ok("A2_obj_units_meters_y_up", aabb.size.is_equal_approx(Vector3(1, 1, 1)) and aabb.position.is_equal_approx(Vector3.ZERO), str(aabb))
	var e := _write_glb(Lib.MODEL_DIR.path_join("flower.glb"), _box_mesh())
	Lib.clear_cache()
	var f1 := Lib.model("flower")
	_ok("A3_glb_written", e == OK, "err=%d" % e)
	_ok("A3_glb_override_picked_up", f1["placeholder"] == false and f1["tris"] == 12, "path=%s tris=%d" % [f1["path"], f1["tris"]])
	var fa: AABB = (f1["mesh"] as Mesh).get_aabb()
	_ok("A3_glb_node_transform_baked", is_equal_approx(fa.position.y, 0.0) and is_equal_approx(fa.end.y, 1.0), str(fa))
	_write_glb(Lib.MODEL_DIR.path_join("heater.glb"), _box_mesh())
	Lib.clear_cache()
	var h2 := Lib.model("heater")
	_ok("A4_glb_preferred_over_obj", h2["path"].ends_with(".glb") and h2["tris"] == 12, h2["path"])
	var img := Image.create(32, 64, false, Image.FORMAT_RGBA8)
	img.fill(Color(1, 0, 0, 1))
	var cpath := Lib.SPRITE_DIR.path_join("character_front.png")
	var ipath := Lib.ICON_DIR.path_join("tool_hoe.png")
	var had_c := FileAccess.file_exists(cpath)
	var had_i := FileAccess.file_exists(ipath)
	if not had_c:
		img.save_png(ProjectSettings.globalize_path(cpath))
		made.append(cpath)
	if not had_i:
		img.save_png(ProjectSettings.globalize_path(ipath))
		made.append(ipath)
	Lib.clear_cache()
	var c1 := Lib.character("front", 0)
	_ok("A5_character_png_slot", had_c or (c1["placeholder"] == false and (c1["texture"] as Texture2D).get_height() == 64))
	_ok("A5_tool_icon_png_slot", had_i or Lib.tool_icon("hoe").get_width() == 32)
	_ok("A5_other_character_dirs_still_placeholder", Lib.character("back", 0)["placeholder"] == true)
	for p in made:
		DirAccess.remove_absolute(ProjectSettings.globalize_path(p))
	var left := made.filter(func(p): return FileAccess.file_exists(p))
	Lib.clear_cache()
	_ok("A6_test_files_removed", left.is_empty(), str(left))
	_ok("A6_back_to_placeholder", Lib.model("heater")["placeholder"] == true and Lib.model("flower")["placeholder"] == true and Lib.character("front", 0)["placeholder"] == true)


func part_b() -> void:
	var e := _export(_make_unskinned(), TEST_PATH)
	_ok("B_unskinned_glb_written", e == OK, "err=%d" % e)
	var r: Node3D = Rig.new()
	get_root().add_child(r)
	_ok("B_unskinned_loaded", r.load_model(TEST_PATH), "clips=%s" % [r.clip_names()])
	r.set_facing(Vector2i(1, 0))
	_ok("B_facing_right_turns_front_to_plus_x", r.transform.basis.z.is_equal_approx(Vector3(1, 0, 0)), str(r.transform.basis.z))
	r.set_facing(Vector2i(0, -1))
	_ok("B_facing_entrance_turns_front_to_minus_z", r.transform.basis.z.is_equal_approx(Vector3(0, 0, -1)), str(r.transform.basis.z))
	var c_walk: String = r.play_state("walk")
	var body: Node3D = r.model.find_child("Body", true, false)
	var y0: float = body.position.y if body else -1.0
	r.anim.advance(0.25)
	var y1: float = body.position.y if body else -1.0
	_ok("B_walk_plays_and_moves_node", c_walk == "walk" and body != null and y1 > y0 + 0.05, "y %.3f -> %.3f" % [y0, y1])
	var c_run: String = r.play_state("run", 1.75)
	_ok("B_run_falls_back_to_fast_walk", c_run == "walk" and is_equal_approx(r.anim.speed_scale, 1.75), "clip=%s speed=%.2f" % [c_run, r.anim.speed_scale])
	_ok("B_idle_missing_stops_gracefully", r.play_state("idle") == "" and not r.anim.is_playing())
	_ok("B_work_missing_stops_gracefully", r.play_state("work") == "" and not r.anim.is_playing())
	r.free()
	DirAccess.remove_absolute(ProjectSettings.globalize_path(TEST_PATH))
	e = _export(_make_skinned(), TEST_PATH)
	_ok("B_skinned_glb_written", e == OK, "err=%d" % e)
	var r2: Node3D = Rig.new()
	get_root().add_child(r2)
	var loaded: bool = r2.load_model(TEST_PATH)
	var sk: Skeleton3D = _find(r2.model, "Skeleton3D") if loaded else null
	var mi: MeshInstance3D = _find(r2.model, "MeshInstance3D") if loaded else null
	_ok("B_loaded_with_skeleton_and_skin", loaded and sk != null and sk.get_bone_count() >= 2 and mi != null and mi.skin != null,
			"bones=%d clips=%s" % [sk.get_bone_count() if sk else -1, r2.clip_names()])
	var names: PackedStringArray = r2.clip_names()
	_ok("B_clips_idle_walk_work", names.has("idle") and names.has("walk") and names.has("work"), str(names))
	if sk != null:
		var bi := sk.find_bone("upper")
		var q0: Quaternion = sk.get_bone_pose_rotation(bi) if bi >= 0 else Quaternion.IDENTITY
		_ok("B_work_clip_chosen", r2.play_state("work") == "work")
		r2.anim.advance(0.25)
		var q1: Quaternion = sk.get_bone_pose_rotation(bi) if bi >= 0 else Quaternion.IDENTITY
		_ok("B_work_animation_rotates_bone", q0.angle_to(q1) > 0.5, "angle=%.3f rad" % q0.angle_to(q1))
		_ok("B_run_without_run_clip_uses_walk", r2.play_state("run", 1.75) == "walk")
		_ok("B_idle_walk_loop", r2.anim.get_animation("idle").loop_mode == Animation.LOOP_LINEAR and r2.anim.get_animation("walk").loop_mode == Animation.LOOP_LINEAR)
	r2.free()
	DirAccess.remove_absolute(ProjectSettings.globalize_path(TEST_PATH))
	_ok("B_cleanup_removed", not FileAccess.file_exists(TEST_PATH))


func part_c() -> void:
	# 저장소의 디렉터 모델
	_ok("C_character_glb_in_repo", Rig.model_exists(), Rig.MODEL_PATH)
	var r: Node3D = Rig.new()
	get_root().add_child(r)
	var ok: bool = r.load_model()
	var names: PackedStringArray = r.clip_names()
	print("character.glb clips=%s" % [names])
	# idle·walk는 꼭 있어야 하고, work·run은 없으면 정해진 순서(work → idle, run → 빠른 walk)로 대신해요
	_ok("C_character_glb_loads_with_idle_and_walk", ok and names.has("idle") and names.has("walk"), str(names))
	var work_clip: String = r.play_state("work") if ok else ""
	var run_clip: String = r.play_state("run", 1.75) if ok else ""
	_ok("C_character_glb_plays_each_state", ok and r.play_state("idle") == "idle" and r.play_state("walk") == "walk"
			and work_clip == ("work" if names.has("work") else "idle") and run_clip == ("run" if names.has("run") else "walk"),
			"work->%s run->%s" % [work_clip, run_clip])
	if ok and not names.has("work"):
		print("NOTE character.glb has no 'work' clip: the work state plays '%s' (spec 2.2 lists idle, walk, work)" % work_clip)
	r.free()
	# 화면: 있으면 그 모델(평면 그림 숨김), 없으면 평면 그림
	for present in [true, false]:
		Rig.model_path = Rig.MODEL_PATH if present else "res://art/models/c13_no_such_character.glb"
		var s: Object = Driver.new_session("direct_play", false)
		var view: Node3D = HouseView3D.new()
		get_root().add_child(view)
		await process_frame
		view.setup(s)
		view.update_dynamic(0.016)
		if present:
			_ok("C_screen_uses_model_when_present", view.rig != null and not view._char.visible)
		else:
			_ok("C_screen_falls_back_to_flat_sprite_when_missing", view.rig == null and view._char.visible and view._char.texture != null)
		view.free()
		s.dispose()
		await process_frame
	Rig.model_path = Rig.MODEL_PATH
