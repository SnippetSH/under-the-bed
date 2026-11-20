# 화면 (나): 3D 모델(포기·구조물·장치) + 서 있는 평면 그림 캐릭터 + 원근 카메라 (M6b 명세 2장). 그리기만 해요.
# 읽는 것: CharSession의 캐릭터·표시, SimWorld의 보이는 것(get_tile_visuals). 진짜 값 접근자(get_true 계열)는 안 불러요.
# 좌표: 바닥 (x, z) → Vector3(x, 높이, z). 카메라는 안쪽 끝(큰 z) 쪽에서 출입구(z 0 쪽)를 바라봐요.
# P7: 모델은 art_library.gd가 art/models/에서 찾고, 없으면 코드 자리 채움 모델을 써요. 포기 640개는 MultiMesh로 그려요.
extends Node3D

const Cfg = preload("res://scripts/cfg.gd")
const Art = preload("res://scripts/plant_art.gd")      # 칸 상태 → 그림 열쇠(key_for)·포기 자리만 같이 써요
const Soft = preload("res://scripts/soft_art.gd")
const Lib = preload("res://scripts/art_library.gd")
const Tools = preload("res://scripts/char_tools.gd")
const HouseCover = preload("res://scripts/house_cover.gd")
const CharacterRig = preload("res://scripts/character_rig.gd")
const DayLight = preload("res://scripts/day_light.gd")

const MARKER_KIND := {
	"watering_can": "water", "fertilizer": "powder", "compost": "powder", "lime": "powder", "hoe": "furrow",
	"mulch": "sheen", "disinfectant": "powder", "inoculant": "powder", "scissors": "petals", "pull": "dust",
	"predators": "bugs",
}
const PLANT_MM_SLOTS := ["plant_stage1", "plant_stage2", "plant_stage3", "plant_stage4", "plant_dead", "flower", "fruit_green", "fruit_red"]

var session: Object
var camera: Camera3D
var env: Environment
var sun: DirectionalLight3D
var cover: Node3D
var ground: MeshInstance3D       # P18 바깥 땅
var sky_mat: ProceduralSkyMaterial
var _fans: Array = []            # P15 천장 유동팬: [{blades: Node3D, speed: float}]
var rig: Node3D = null            # art/models/character.glb가 있으면 3D 캐릭터
var _mm := {}                    # 자리 -> MultiMeshInstance3D
var _mulch_mm := {}              # 멀칭 종류 -> MultiMeshInstance3D
var _plant_xf: Array = []        # 포기마다 기본 변환 (자리 + 꾸밈 회전·크기)
var _addon_off := {}             # "flower"/"fruit_green"/"fruit_red" -> 단계별 [Vector3...]
var _fruit_tilt := 1.2
var _per := 8
var _last_v: Dictionary = {}
var _char: Sprite3D
var _char_shadow: Sprite3D
var _arrow: MeshInstance3D
var _frame: Sprite3D
var _markers: Array = []
var _marker_born := {}
var _device_labels := {}
var _bed_h := 0.15
var _cam_h := 4.2
var _cam_d := 4.6
var _look := 1.2
var _time := 0.0


static func v3(p: Vector2, y := 0.0) -> Vector3:
	return Vector3(p.x, y, p.y)


static func _h01(i: int, salt: int) -> float:
	var x := (i * 73856093) ^ (salt * 19349663)
	x = (x ^ (x >> 13)) * 1274126177
	return float(absi(x) % 10007) / 10007.0


func _mat(c: Color, tex: Texture2D = null) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = c
	m.roughness = 0.95
	m.specular_mode = BaseMaterial3D.SPECULAR_DISABLED
	m.diffuse_mode = BaseMaterial3D.DIFFUSE_LAMBERT_WRAP
	if tex != null:
		m.albedo_texture = tex
		m.texture_filter = BaseMaterial3D.TEXTURE_FILTER_LINEAR_WITH_MIPMAPS_ANISOTROPIC
	if c.a < 1.0:
		m.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		m.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	m.cull_mode = BaseMaterial3D.CULL_DISABLED
	return m


func _decal(tex: Texture2D, size_m: float, pos: Vector3, priority := 0, on_top := false) -> Sprite3D:
	var sp := Sprite3D.new()
	sp.texture = tex
	sp.axis = Vector3.AXIS_Y
	sp.pixel_size = size_m / float(tex.get_width())
	sp.shaded = false
	sp.double_sided = true
	sp.alpha_cut = SpriteBase3D.ALPHA_CUT_DISABLED
	sp.texture_filter = BaseMaterial3D.TEXTURE_FILTER_LINEAR_WITH_MIPMAPS
	sp.render_priority = priority
	sp.no_depth_test = on_top
	sp.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	sp.position = pos
	add_child(sp)
	return sp


func _multimesh(mesh: Mesh, capacity: int) -> MultiMeshInstance3D:
	var mm := MultiMesh.new()
	mm.transform_format = MultiMesh.TRANSFORM_3D
	mm.mesh = mesh
	mm.instance_count = capacity
	mm.visible_instance_count = 0
	var mi := MultiMeshInstance3D.new()
	mi.multimesh = mm
	add_child(mi)
	return mi


func _instance(slot: String, xf: Transform3D) -> MeshInstance3D:
	var m := Lib.model(slot)
	var mi := MeshInstance3D.new()
	mi.mesh = m["mesh"]
	mi.transform = xf
	add_child(mi)
	return mi


func setup(s: Object) -> void:
	session = s
	_bed_h = float(Cfg.get_v("layout", "bed_height_m"))
	_cam_h = float(Cfg.get_v("camera3d", "height_m"))
	_cam_d = float(Cfg.get_v("camera3d", "distance_m"))
	_look = float(Cfg.get_v("camera3d", "look_ahead_m"))
	_per = int(Cfg.get_v("plant_art", "plants_per_cell"))
	_fruit_tilt = deg_to_rad(float(Cfg.get_v("plant_models", "fruit_tilt_deg")))
	for kind in ["flower", "fruit_green", "fruit_red"]:
		var per_stage := []
		for st in range(1, 5):
			var arr := []
			for o in Cfg.get_v("plant_models", "%s_offsets_stage%d" % [kind, st]):
				arr.append(Vector3(o[0], o[1], o[2]))
			per_stage.append(arr)
		_addon_off[kind] = per_stage
	var L: Object = s.layout
	var total_z: float = L.front_m + L.length_m + L.back_m
	var zc: float = (L.length_m + L.back_m - L.front_m) * 0.5
	var n_cells: int = L.rows * L.cols
	# 바닥: aisle_floor 모델이 있으면 1 m 타일로 깔고, 없으면 흙길 무늬 평면
	var floor_m := Lib.model("aisle_floor")
	if not floor_m.is_empty():
		var nx := int(ceil(L.width_m + 4.0))
		var nz := int(ceil(total_z + 6.0))
		var fmi := _multimesh(floor_m["mesh"], nx * nz)
		var k := 0
		for ix in range(nx):
			for iz in range(nz):
				fmi.multimesh.set_instance_transform(k, Transform3D(Basis.IDENTITY, Vector3(-2.0 + ix + 0.5, 0, -L.front_m - 3.0 + iz + 0.5)))
				k += 1
		fmi.multimesh.visible_instance_count = k
	else:
		var floor_tex := Soft.noise_texture("floor", Color("#dcc6a0"), Color("#bfa178"), 0.035)
		var fm := _mat(Color(1, 1, 1), floor_tex)
		fm.uv1_scale = Vector3((L.width_m + 8.0) / 2.5, (total_z + 10.0) / 2.5, 1)
		var pl := MeshInstance3D.new()
		var pm := PlaneMesh.new()
		pm.size = Vector2(L.width_m + 0.3, total_z + 0.3)   # P18: 흙길은 하우스 안만
		pl.mesh = pm
		pl.material_override = fm
		pl.position = Vector3(L.width_m * 0.5, 0.0, zc)
		add_child(pl)
	# P18 바깥 땅: 아주 큰 평면 하나(같은 무늬 방식, 풀빛 흙). 안개로 지평선에 녹아들어 끝이 보이지 않아요(꾸밈)
	var ground_size := float(Cfg.get_v("outside", "ground_size_m"))
	var gtex := Soft.noise_texture("ground", Color(str(Cfg.get_v("outside", "ground_color_a"))), Color(str(Cfg.get_v("outside", "ground_color_b"))), 0.03)
	var gm := _mat(Color(1, 1, 1), gtex)
	gm.uv1_scale = Vector3(ground_size / 3.0, ground_size / 3.0, 1)
	ground = MeshInstance3D.new()
	var gpm := PlaneMesh.new()
	gpm.size = Vector2(ground_size, ground_size)
	ground.mesh = gpm
	ground.material_override = gm
	ground.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	ground.position = Vector3(L.width_m * 0.5, -0.01, zc)
	add_child(ground)
	# 베드: 칸마다 1 m 베드 구간
	var bed_mi := _multimesh(Lib.model("bed")["mesh"], n_cells)
	for i in range(n_cells):
		bed_mi.multimesh.set_instance_transform(i, Transform3D(Basis.IDENTITY, v3(L.cell_rect(i).get_center(), 0.0)))
	bed_mi.multimesh.visible_instance_count = n_cells
	# 멀칭 (종류마다 MultiMesh)
	for kind in Lib.MULCH_SLOT_OF:
		_mulch_mm[kind] = _multimesh(Lib.model(Lib.MULCH_SLOT_OF[kind])["mesh"], n_cells)
	# 비닐하우스: 1 m 구간을 길이 방향으로 반복 (폭은 기준 폭에 맞춰 늘려요)
	var frame_m := Lib.model("house_frame")
	var n_sec := int(ceil(total_z))
	var hmi := _multimesh(frame_m["mesh"], n_sec)
	var sx: float = L.width_m / Lib.REFERENCE_HOUSE_WIDTH_M
	for i in range(n_sec):
		hmi.multimesh.set_instance_transform(i, Transform3D(Basis.IDENTITY.scaled(Vector3(sx, 1, 1)), Vector3(L.width_m * 0.5, 0, -L.front_m + i + 0.5)))
	hmi.multimesh.visible_instance_count = n_sec
	hmi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	_build_gable(L)
	# P15·P16 천장 유동팬 (몸통 + 도는 날개). 출입구 쪽 한 대는 유동팬 스위치 바로 위(장치 줄 위, 장치 이름표보다 높게),
	# 나머지는 베드 구간 가운데 통로 위에 고르게. 카메라 쪽으로 다가오는 팬은 위치에 따라 서서히 투명해져요(update_fans)
	var fan_y := float(Cfg.get_v("fans", "height_m"))
	var fan_n := int(Cfg.get_v("fans", "count"))
	# P17: 모든 팬을 하우스 가운데 줄(가운데 통로 위) 한 줄로 고르게. 첫 팬은 출입구 쪽(장치 줄 앞), 마지막은 안쪽 끝 가까이
	var spots := []
	var z_first := float(Cfg.get_v("fans", "entrance_z_m"))
	var z_last: float = L.length_m + float(Cfg.get_v("fans", "last_z_from_end_m"))
	var total := fan_n + 1
	for i in range(total):
		var z: float = lerpf(z_first, z_last, float(i) / float(total - 1))
		var y := float(Cfg.get_v("fans", "entrance_height_m")) if i == 0 else fan_y
		spots.append(Vector3(L.width_m * 0.5, y, z))
	for spot in spots:
		var fan_root := Node3D.new()
		fan_root.position = spot
		fan_root.scale = Vector3.ONE * float(Cfg.get_v("fans", "scale"))
		add_child(fan_root)
		var body := MeshInstance3D.new()
		body.mesh = Lib.model("ceiling_fan")["mesh"]
		body.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		fan_root.add_child(body)
		var blades := MeshInstance3D.new()
		blades.mesh = Lib.model("ceiling_fan_blades")["mesh"]
		blades.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		fan_root.add_child(blades)
		_fans.append({"blades": blades, "speed": 0.0, "root": fan_root, "body": body, "opacity": 1.0})
	# 피복 비닐 겹·측창 (상태대로, 매 프레임 sync)
	cover = HouseCover.new()
	add_child(cover)
	cover.setup(L)
	cover.sync(s.world().get_house_controls(), 10.0)
	# 포기 자리 (MultiMesh: 단계·시든 것·꽃·열매마다)
	var offs: Array = Art.plant_offsets(_per)
	var yaw_j := deg_to_rad(float(Cfg.get_v("plant_models", "yaw_jitter_deg")))
	var sc_j := float(Cfg.get_v("plant_models", "scale_jitter"))
	_plant_xf.clear()
	for i in range(n_cells):
		var cr: Rect2 = L.cell_rect(i)
		for k in range(offs.size()):
			var idx := i * _per + k
			var o: Vector2 = offs[k]
			var yaw := (_h01(idx, 1) * 2.0 - 1.0) * yaw_j
			var sc := 1.0 + (_h01(idx, 2) * 2.0 - 1.0) * sc_j
			_plant_xf.append(Transform3D(Basis(Vector3.UP, yaw).scaled(Vector3(sc, sc, sc)), v3(cr.position + o * cr.size, _bed_h)))
	_build_plant_multimeshes()
	# 장치 (앞면 +Z = 카메라 쪽)
	var font := _font()
	for dev in L.devices:
		var r: Rect2 = dev["rect"]
		var slot: String = Lib.DEVICE_SLOT_OF[dev["kind"]]
		var at := Vector3(r.get_center().x, 0.0, r.get_center().y)
		var yaw := 0.0
		var mi := _instance(slot, Transform3D(Basis(Vector3.UP, yaw), at))
		var top: float = mi.get_aabb().end.y
		var lb := Label3D.new()
		lb.text = dev["label"]
		lb.font = font
		lb.billboard = BaseMaterial3D.BILLBOARD_ENABLED
		lb.pixel_size = 0.0035
		lb.font_size = 34
		lb.outline_size = 12
		lb.outline_modulate = Color(0.25, 0.17, 0.1, 0.85)
		lb.no_depth_test = true
		lb.position = at + Vector3(0, top + 0.22, 0)
		add_child(lb)
		_device_labels[dev["id"]] = lb
	# 캐릭터 (평면 그림) + 그림자, 바라보는 방향 표시, 대상 칸 빛
	_char_shadow = _decal(Soft.shadow_texture(), 0.62, Vector3.ZERO, 4)
	_char_shadow.scale = Vector3(1, 1, 0.6)
	_char = Sprite3D.new()
	_char.shaded = false
	_char.double_sided = true
	_char.alpha_cut = SpriteBase3D.ALPHA_CUT_OPAQUE_PREPASS
	_char.texture_filter = BaseMaterial3D.TEXTURE_FILTER_LINEAR_WITH_MIPMAPS
	_char.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(_char)
	if CharacterRig.model_exists():
		rig = CharacterRig.new()
		add_child(rig)
		if rig.load_model():
			_char.visible = false
		else:
			rig.queue_free()
			rig = null
	_arrow = MeshInstance3D.new()
	var am := ArrayMesh.new()
	var arrs := []
	arrs.resize(Mesh.ARRAY_MAX)
	arrs[Mesh.ARRAY_VERTEX] = PackedVector3Array([Vector3(0, 0, -0.46), Vector3(-0.11, 0, -0.3), Vector3(0.11, 0, -0.3)])
	am.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrs)
	_arrow.mesh = am
	var arm := _mat(Color(1.0, 0.95, 0.75, 0.75))
	arm.render_priority = 5
	_arrow.material_override = arm
	_arrow.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(_arrow)
	_frame = _decal(Soft.target_texture(), L.cell_m * 1.12, Vector3.ZERO, 2, false)   # P15: 깊이 검사 켬 (캐릭터·포기·장치에 가려져요)
	# 카메라, 빛, 하늘
	camera = Camera3D.new()
	camera.fov = float(Cfg.get_v("camera3d", "fov_deg"))
	camera.current = true
	add_child(camera)
	sun = DirectionalLight3D.new()
	sun.shadow_enabled = bool(Cfg.get_v("light", "shadows"))
	sun.shadow_blur = 2.0
	sun.directional_shadow_mode = DirectionalLight3D.SHADOW_PARALLEL_2_SPLITS
	sun.directional_shadow_max_distance = 18.0
	sun.light_angular_distance = 2.0
	add_child(sun)
	var we := WorldEnvironment.new()
	env = Environment.new()
	# P18 하늘: 위아래 그라데이션 하늘(ProceduralSkyMaterial, 해 원반 없이). 색은 시각 빛에 따라 set_sky가 바꿔요
	env.background_mode = Environment.BG_SKY
	sky_mat = ProceduralSkyMaterial.new()
	sky_mat.sun_angle_max = 0.0
	sky_mat.sky_curve = 0.12
	sky_mat.ground_curve = 0.05
	var sky := Sky.new()
	sky.sky_material = sky_mat
	sky.radiance_size = Sky.RADIANCE_SIZE_32
	env.sky = sky
	env.background_color = Color("#cfe3ea")
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color("#dfe9ee")
	env.ambient_light_energy = float(Cfg.get_v("light", "ambient_energy"))
	env.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	env.fog_enabled = true
	env.fog_light_color = Color("#e8eef0")
	env.fog_density = float(Cfg.get_v("outside", "fog_density"))
	env.fog_sky_affect = 0.0
	we.environment = env
	add_child(we)
	set_light(12.0)


func _build_plant_multimeshes() -> void:
	var cap: int = _plant_xf.size()
	for slot in PLANT_MM_SLOTS:
		if _mm.has(slot):
			_mm[slot].queue_free()
		var mult := 1
		if slot == "flower":
			mult = 4
		elif slot.begins_with("fruit"):
			mult = 3
		_mm[slot] = _multimesh(Lib.model(slot)["mesh"], cap * mult)
	_last_v = {}


# 측정용: 자리 채움 포기 메시를 세밀함을 바꿔 다시 만들어요
func rebuild_plants(detail: int) -> int:
	Lib.plant_detail = detail
	for slot in PLANT_MM_SLOTS:
		Lib._cache.erase(slot)
	_build_plant_multimeshes()
	refresh_visuals()
	return int(Lib.model("plant_stage4")["tris"])


static func _font() -> Font:
	var f := SystemFont.new()
	f.font_names = PackedStringArray(["Malgun Gothic", "맑은 고딕", "Apple SD Gothic Neo", "Noto Sans CJK KR", "NanumGothic", "sans-serif"])
	f.antialiasing = TextServer.FONT_ANTIALIASING_GRAY
	f.font_weight = 600
	return f


# 출입구 쪽 박공 비닐 (house_frame 모델과 별도, 코드로 그려요)
func _build_gable(L: Object) -> void:
	var W: float = L.width_m
	var R := W * 0.5
	var z0: float = -L.front_m - 0.02
	var pts := [Vector2(0, 0), Vector2(0, 1.0)]
	for i in range(0, 25):
		var a := PI - PI * i / 24.0
		pts.append(Vector2(R + R * cos(a), 1.0 + R * sin(a)))
	pts.append(Vector2(W, 0))
	var verts := PackedVector3Array()
	var c := Vector3(R, 0.5, z0)
	for k in range(pts.size() - 1):
		verts.append_array([c, Vector3(pts[k].x, pts[k].y, z0), Vector3(pts[k + 1].x, pts[k + 1].y, z0)])
	var am := ArrayMesh.new()
	var arrs := []
	arrs.resize(Mesh.ARRAY_MAX)
	arrs[Mesh.ARRAY_VERTEX] = verts
	am.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrs)
	var mi := MeshInstance3D.new()
	mi.mesh = am
	var gm := _mat(Color(0.9, 0.95, 1.0, 0.26))
	gm.render_priority = -2
	mi.material_override = gm
	mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	add_child(mi)


var preview_plant := false


func refresh_visuals() -> void:
	preview_plant = session.has_held_plant()
	var w: Object = session.world()
	var v: Dictionary = w.get_tile_visuals(float(Cfg.get_v("visual", "ripening_min_index")))
	_last_v = v
	var L: Object = session.layout
	var n_cells: int = L.rows * L.cols
	var counts := {}
	for slot in PLANT_MM_SLOTS:
		counts[slot] = 0
	var mul_counts := {}
	for kind in _mulch_mm:
		mul_counts[kind] = 0
	var mul: PackedStringArray = v["mulch_id"]
	for i in range(n_cells):
		var m: String = mul[i]
		if _mulch_mm.has(m):
			_mulch_mm[m].multimesh.set_instance_transform(mul_counts[m], Transform3D(Basis.IDENTITY, v3(L.cell_rect(i).get_center(), _bed_h)))
			mul_counts[m] += 1
		var key := Art.key_for(v, i)
		# P15 정식 미리 보기: 붙잡아 둔 정식이 있으면 빈 칸·죽은 포기 칸에 가장 작은 모종을 바로 그려요(게임 쪽 표시, 다음 시간에 코어 상태로)
		if preview_plant and (key == "e" or key == "d"):
			key = "p0000"
		if key == "e":
			continue
		var base_slot := "plant_dead"
		var stage := 3
		if key != "d":
			stage = int(key.substr(1, 1)) + 1
			base_slot = "plant_stage%d" % stage
		for k in range(_per):
			var xf: Transform3D = _plant_xf[i * _per + k]
			_put(base_slot, xf, counts)
			if key == "d":
				continue
			# P13: 덧붙임 개수를 칸의 개수에 비례해 나눠 그려요. 칸 개수를 그려진 포기 8개에 고르게 나누고(_share),
			# 포기마다 단계별 자리 수만큼에서 잘라요(많아도 넘치지 않게). 꽃 = 꽃·꽃봉오리, 초록 = 맺힌 열매 − 익어 가는 것, 빨강 = 익어 가는 것
			var fo: Array = _addon_off["flower"][stage - 1]
			for j in range(mini(_share(int(v["open_flower_count"][i]), k), fo.size())):
				_put("flower", xf * Transform3D(Basis.IDENTITY, fo[j]), counts)
			var rp_n: int = int(v["ripening_fruit_count"][i])
			var gr_n: int = maxi(0, int(v["fruit_count"][i]) - rp_n)
			var go: Array = _addon_off["fruit_green"][stage - 1]
			for j in range(mini(_share(gr_n, k), go.size())):
				_put("fruit_green", xf * Transform3D(Basis(Vector3.UP, j * 2.1) * Basis(Vector3.RIGHT, _fruit_tilt), go[j]), counts)
			var ro: Array = _addon_off["fruit_red"][stage - 1]
			for j in range(mini(_share(rp_n, k), ro.size())):
				_put("fruit_red", xf * Transform3D(Basis(Vector3.UP, j * 2.4 + 1.0) * Basis(Vector3.RIGHT, _fruit_tilt), ro[j]), counts)
	for slot in PLANT_MM_SLOTS:
		_mm[slot].multimesh.visible_instance_count = counts[slot]
	for kind in _mulch_mm:
		_mulch_mm[kind].multimesh.visible_instance_count = mul_counts[kind]


# 칸 개수 total을 그려진 포기 _per개에 고르게 나눌 때 k번째 포기 몫 (앞 포기부터 하나씩 더)
func _share(total: int, k: int) -> int:
	return (total + (_per - 1 - k)) / _per


# 검사용: 칸 하나에 그려진 덧붙임 수 {flower, fruit_green, fruit_red}
func drawn_addons_for_cell(cell: int) -> Dictionary:
	var out := {"flower": 0, "fruit_green": 0, "fruit_red": 0}
	if _last_v.is_empty():
		return out
	var key := Art.key_for(_last_v, cell)
	if key == "e" or key == "d":
		return out
	var stage := int(key.substr(1, 1)) + 1
	var rp_n: int = int(_last_v["ripening_fruit_count"][cell])
	var gr_n: int = maxi(0, int(_last_v["fruit_count"][cell]) - rp_n)
	for k in range(_per):
		out["flower"] += mini(_share(int(_last_v["open_flower_count"][cell]), k), _addon_off["flower"][stage - 1].size())
		out["fruit_green"] += mini(_share(gr_n, k), _addon_off["fruit_green"][stage - 1].size())
		out["fruit_red"] += mini(_share(rp_n, k), _addon_off["fruit_red"][stage - 1].size())
	return out


func _put(slot: String, xf: Transform3D, counts: Dictionary) -> void:
	var mm: MultiMesh = _mm[slot].multimesh
	var n: int = counts[slot]
	if n < mm.instance_count:
		mm.set_instance_transform(n, xf)
		counts[slot] = n + 1


func plant_instance_counts() -> Dictionary:
	var out := {}
	for slot in PLANT_MM_SLOTS:
		out[slot] = _mm[slot].multimesh.visible_instance_count
	return out


# 시각(시 + 몫)에 따른 해 (꾸밈, 진짜 값 없음)
func set_light(hour_f: float) -> void:
	apply_light(DayLight.params(hour_f))


# 빛 값(DayLight.params / DayLight.update)으로 해·하늘을 그려요. 자러 가기·며칠 맡기기 동안에는 한 번 도는 표현 값이 와요
func apply_light(p: Dictionary) -> void:
	sun.rotation = Vector3(deg_to_rad(-float(p["sun_elev_deg"])), deg_to_rad(180.0 + float(p["sun_az_deg"])), 0)
	sun.light_color = p["sun_color"]
	sun.light_energy = float(Cfg.get_v("light", "sun_energy_day")) * float(p["sun_energy_factor"])
	set_sky(DayLight.SKY_BASE * (p["tint"] as Color))


# 유동팬 날개: 켜져 있으면 정한 속도까지 서서히 빨라지고, 꺼지면 서서히 멈춰요 (보이는 것 fan_on만 봐요)
func update_fans(on: bool, delta: float) -> void:
	var top := float(Cfg.get_v("fans", "spin_rad_per_s"))
	var ramp := maxf(0.01, float(Cfg.get_v("fans", "spin_up_seconds")))
	var cz: float = session.pos.y
	var f0 := float(Cfg.get_v("fans", "fade_gone_m"))
	var f1 := float(Cfg.get_v("fans", "fade_full_m"))
	for f in _fans:
		# P16: 보이기·숨기기를 끄고 켜지 않고, 캐릭터와 팬 사이 거리(u = 캐릭터 z − 팬 z, 양수 = 팬이 캐릭터 너머)로 불투명도를 매끄럽게 정해요.
		# 팬이 캐릭터를 지나 카메라 쪽으로 올수록 서서히 사라지고(카메라와 캐릭터 사이에서 가리지 않게), 되돌아가면 서서히 나타나요.
		# 위치만의 함수라 걷기·달리기 속도와 무관하게 튀지 않아요. 팬은 그림자를 드리우지 않아서 남는 그림자도 없어요
		var u: float = cz - (f["root"] as Node3D).position.z
		var op := smoothstep(f0, f1, u)
		f["opacity"] = op
		var tr := 1.0 - op
		(f["body"] as GeometryInstance3D).transparency = tr
		(f["blades"] as GeometryInstance3D).transparency = tr
		(f["root"] as Node3D).visible = op > 0.002   # 완전히 투명할 때만 그리기를 건너뛰어요(눈에 보이는 차이 없음)
		f["speed"] = move_toward(float(f["speed"]), top if on else 0.0, top * delta / ramp)
		(f["blades"] as Node3D).rotation.z += float(f["speed"]) * delta


func fan_opacities() -> Array:
	return _fans.map(func(f): return float(f["opacity"]))


func fan_spin_speeds() -> Array:
	return _fans.map(func(f): return snappedf(float(f["speed"]), 0.01))


func set_sky(c: Color) -> void:
	env.background_color = c
	# 하늘: 위는 짙게, 지평선은 밝게. 밤에는 c 자체가 어두운 남색이라 하늘도 어두워요. 지평선·땅 끝은 안개 색과 같아서 경계가 안 보여요
	# 밤에는 지평선·안개도 어둡게 (밝은 낮일수록 흰빛을 더 섞어요)
	var bright := clampf((c.get_luminance() - 0.3) / 0.5, 0.0, 1.0)
	var horizon := c.lerp(Color.WHITE, 0.35 * bright).darkened(0.25 * (1.0 - bright))
	sky_mat.sky_top_color = c.darkened(0.25)
	sky_mat.sky_horizon_color = horizon
	sky_mat.ground_horizon_color = horizon
	sky_mat.ground_bottom_color = horizon.darkened(0.2)
	env.fog_light_color = horizon
	env.ambient_light_color = c.lerp(Color.WHITE, 0.3)


func update_dynamic(delta := 0.016) -> void:
	_time += delta
	var s: Object = session
	var p: Vector2 = s.pos
	var dir := "front" if s.facing.y > 0 else ("back" if s.facing.y < 0 else "side")
	if s.facing.x != 0 and s.facing.y != 0:
		dir = "side"   # 대각선은 옆모습 (옆 베드를 보는 일이 많아요)
	var frame := 0
	var bob := 0.0
	if s.moving:
		var ph: float = s.walk_distance / 0.32
		frame = int(ph) % 2
		bob = absf(sin(ph * PI)) * 0.035
	if rig != null:
		rig.position = v3(p, 0.0)
		rig.set_facing(s.facing)
		var state := "idle"
		if s.acting and not s.moving:
			state = "work"
		elif s.running:
			state = "run"
		elif s.moving:
			state = "walk"
		rig.play_state(state, s.run_factor)
	var ch := Lib.character(dir, frame)
	var tex: Texture2D = ch["texture"]
	if _char.texture != tex:
		_char.texture = tex
		var hpx := float(tex.get_height())
		_char.pixel_size = float(Cfg.get_v("plant_art", "character_height_m")) / hpx
		_char.offset = Vector2(0, hpx * (0.5 - float(ch["base_frac"])))
	_char.flip_h = s.facing.x < 0
	_char.position = v3(p, bob)
	_char_shadow.position = v3(p, 0.006)
	var shs := 1.0 - bob * 3.0
	_char_shadow.scale = Vector3(shs, 1, 0.6 * shs)
	_arrow.position = v3(p, 0.012)
	_arrow.rotation = Vector3(0, atan2(-float(s.facing.x), -float(s.facing.y)), 0)
	var tc: int = s.target_cell() if s.can_act() else -1
	_frame.visible = tc >= 0
	if tc >= 0:
		_frame.position = v3(s.layout.cell_center(tc), _bed_h + 0.02)
		_frame.modulate = Color(1, 1, 1, 0.75 + 0.25 * sin(_time * 4.0))
	var dev: Dictionary = s.device_in_front() if (tc < 0 and s.can_act()) else {}
	var near := float(Cfg.get_v("camera3d", "label_distance_m"))
	for id in _device_labels:
		var lb: Label3D = _device_labels[id]
		var hot: bool = dev.get("id", "") == id
		lb.modulate = Color("#ffe27a") if hot else Color(1, 0.98, 0.94)
		lb.visible = hot or Vector2(lb.position.x, lb.position.z).distance_to(p) <= near
	_update_markers()
	var hc: Dictionary = s.world().get_house_controls()
	cover.sync(hc, delta)
	update_fans(bool(hc["fan_on"]), delta)
	# 카메라가 비닐 아치 밖으로 나가지 않게 옆으로는 하우스 가운데 쪽에 머물러요
	var W: float = s.layout.width_m
	var cx := clampf(p.x, minf(W * 0.5, 1.5), maxf(W * 0.5, W - 1.5))
	camera.position = Vector3(cx, _cam_h, p.y + _cam_d)
	camera.look_at(v3(p, 0.0) + Vector3(0, 0, -_look), Vector3.UP)


func _update_markers() -> void:
	var cells: Array = session.markers.keys()
	for c in _marker_born.keys():
		if not session.markers.has(c):
			_marker_born.erase(c)
	while _markers.size() < cells.size():
		_markers.append(_decal(Soft.marker_texture("water", Color.WHITE), 0.9, Vector3.ZERO, 1, false))   # P15: 베드 위에 눕고 캐릭터에 가려져요
	for j in range(_markers.size()):
		var sp: Sprite3D = _markers[j]
		if j >= cells.size():
			sp.visible = false
			continue
		var c: int = cells[j]
		if not _marker_born.has(c):
			_marker_born[c] = _time
		var t: Dictionary = Tools.TOOLS[session.markers[c][-1]]
		sp.texture = Soft.marker_texture(MARKER_KIND.get(t["id"], "powder"), Color(t["marker"]))
		sp.pixel_size = 0.9 / float(sp.texture.get_width())
		var age := clampf((_time - float(_marker_born[c])) / 0.3, 0.0, 1.0)
		var sc := lerpf(0.6, 1.0, ease(age, 0.4))
		sp.scale = Vector3(sc, 1, sc)
		sp.modulate = Color(1, 1, 1, age)
		sp.position = v3(session.layout.cell_center(c), _bed_h + 0.03)
		sp.visible = true
