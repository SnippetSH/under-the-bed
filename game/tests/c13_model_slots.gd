# C13 (m6b-character-play 9.1, 2.2): 모델 자리. 모델 파일이 있을 때와 없을 때, 캐릭터 모델과 동작 이름.
#  A) 그림 자리(art_library): 파일이 없으면 코드로 만든 모델, .obj(+.mtl)·.glb를 넣으면 그 모델, 둘 다 있으면 .glb,
#     캐릭터·도구 PNG 자리, 지운 뒤 다시 코드 모델. 시험 파일은 직접 만들고(내려받기 없음) 끝나면 지워요.
#  B) 캐릭터 리그(character_rig): 뼈대 없는 모델 + 노드 애니메이션 "walk"만, 뼈대·스킨 모델 + "idle"·"walk"·"work"
#     (시험 .glb는 user://에 써요. 저장소의 character.glb는 건드리지 않아요)
#  C) 저장소의 character.glb(디렉터 모델): 읽히고 동작 idle·walk·work가 있음, 화면이 그 모델을 써요(평면 그림 숨김).
#     자리에 파일이 없으면 화면은 평면 그림으로 돌아가요
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
	U.finish(self, fails)


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
