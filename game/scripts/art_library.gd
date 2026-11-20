# 그림 자리(slot) 불러오기 (M6b P7). art/models/<자리>.glb → .obj(+.mtl) 순서로 찾고, 없으면 코드 자리 채움 모델을 써요.
# 파일만 넣으면 코드 수정 없이 모양이 바뀌어요. 규약은 game/art/README.md.
# 실행 중에 직접 읽어요(.glb는 GLTFDocument, .obj는 아래의 작은 해석기) — 편집기 가져오기(import)를 거치지 않아요.
extends RefCounted

const Cfg = preload("res://scripts/cfg.gd")
const Placeholder = preload("res://scripts/placeholder_meshes.gd")
const Soft = preload("res://scripts/soft_art.gd")

const MODEL_DIR := "res://art/models"
const SPRITE_DIR := "res://art/sprites"
const ICON_DIR := "res://art/icons"

const PLANT_SLOTS := ["plant_stage1", "plant_stage2", "plant_stage3", "plant_stage4", "plant_dead", "flower", "fruit_green", "fruit_red"]
const STRUCTURE_SLOTS := ["house_frame", "bed", "mulch_clear", "mulch_black", "mulch_white", "aisle_floor"]
const DEVICE_SLOTS := ["heater", "fan_switch", "window_panel", "workbench_seedlings", "workbench_cover", "door", "drip_valve", "irrigation_controller", "ceiling_fan", "ceiling_fan_blades"]
const CHARACTER_SPRITES := ["character_front", "character_back", "character_side"]
# 장치 종류(house_layout) → 모델 자리
const DEVICE_SLOT_OF := {
	"heater": "heater", "fan": "fan_switch", "window_panel": "window_panel", "bench_plant": "workbench_seedlings",
	"bench_cover": "workbench_cover", "door": "door", "drip": "drip_valve", "auto_irrigation": "irrigation_controller",
}
const MULCH_SLOT_OF := {"clear_film": "mulch_clear", "black_film": "mulch_black", "white_film": "mulch_white"}
const REFERENCE_HOUSE_WIDTH_M := 6.7   # house_frame 모델의 기준 폭 (기본 배치의 폭)

static var _cache := {}
static var plant_detail := 1           # 자리 채움 포기의 세밀함 (프레임 시간 측정용)


static func clear_cache() -> void:
	_cache.clear()


static func model_path(slot: String) -> String:
	for ext in ["glb", "obj"]:
		var p := MODEL_DIR.path_join("%s.%s" % [slot, ext])
		if FileAccess.file_exists(p):
			return p
	return ""


# {mesh: Mesh, placeholder: bool, path: String, tris: int}
static func model(slot: String) -> Dictionary:
	if _cache.has(slot):
		return _cache[slot]
	var path := model_path(slot)
	var r := {}
	if path != "":
		var m: ArrayMesh = _load_glb(path) if path.ends_with(".glb") else _load_obj(path)
		if m != null and m.get_surface_count() > 0:
			r = {"mesh": m, "placeholder": false, "path": path, "tris": count_tris(m)}
		else:
			push_warning("모델을 읽지 못해서 자리 채움 모델을 써요: %s" % path)
	if r.is_empty():
		var ph := _placeholder(slot)
		if ph.is_empty():
			return {}
		r = {"mesh": ph["mesh"], "placeholder": true, "path": "", "tris": ph["tris"]}
	_cache[slot] = r
	return r


static func _placeholder(slot: String) -> Dictionary:
	match slot:
		"plant_stage1", "plant_stage2", "plant_stage3", "plant_stage4":
			return Placeholder.plant(int(slot.substr(11, 1)), plant_detail)
		"plant_dead":
			return Placeholder.plant(3, plant_detail, true)
		"flower":
			return Placeholder.flower()
		"fruit_green":
			return Placeholder.fruit(false)
		"fruit_red":
			return Placeholder.fruit(true)
		"bed":
			return Placeholder.bed(float(Cfg.get_v("layout", "bed_height_m")), Soft.noise_texture("soil", Color("#8a6040"), Color("#5e3e24"), 0.06))
		"mulch_clear":
			return Placeholder.mulch("clear_film")
		"mulch_black":
			return Placeholder.mulch("black_film")
		"mulch_white":
			return Placeholder.mulch("white_film")
		"house_frame":
			return Placeholder.house_frame(REFERENCE_HOUSE_WIDTH_M)
		"aisle_floor":
			return {}   # 선택 자리: 없으면 바닥 무늬 평면을 그려요
	if slot in DEVICE_SLOTS:
		return Placeholder.device(slot)
	return {}


static func count_tris(m: Mesh) -> int:
	var n := 0
	for s in range(m.get_surface_count()):
		var arr := m.surface_get_arrays(s)
		var idx: PackedInt32Array = arr[Mesh.ARRAY_INDEX] if arr[Mesh.ARRAY_INDEX] != null else PackedInt32Array()
		n += (idx.size() if idx.size() > 0 else (arr[Mesh.ARRAY_VERTEX] as PackedVector3Array).size()) / 3
	return n


# ---------- .glb (glTF 2.0 바이너리) ----------
static func _load_glb(path: String) -> ArrayMesh:
	var doc := GLTFDocument.new()
	var st := GLTFState.new()
	var e := doc.append_from_file(ProjectSettings.globalize_path(path), st)
	if e != OK:
		push_warning("glb 읽기 실패 %s (%d)" % [path, e])
		return null
	var root := doc.generate_scene(st)
	if root == null:
		return null
	var out := ArrayMesh.new()
	_bake(root, Transform3D.IDENTITY, out)
	root.free()
	return out


# 장면 안의 메시를 원점 기준 변환을 적용해 한 메시로 합쳐요 (재질은 면마다 유지)
static func _bake(node: Node, parent_xf: Transform3D, out: ArrayMesh) -> void:
	var xf := parent_xf
	if node is Node3D:
		xf = parent_xf * (node as Node3D).transform
	if node is MeshInstance3D and (node as MeshInstance3D).mesh != null:
		var mi := node as MeshInstance3D
		var mesh := mi.mesh
		for s in range(mesh.get_surface_count()):
			var arr := mesh.surface_get_arrays(s)
			var v: PackedVector3Array = arr[Mesh.ARRAY_VERTEX]
			for i in range(v.size()):
				v[i] = xf * v[i]
			arr[Mesh.ARRAY_VERTEX] = v
			if arr[Mesh.ARRAY_NORMAL] != null:
				var nr: PackedVector3Array = arr[Mesh.ARRAY_NORMAL]
				var nb := xf.basis.inverse().transposed()
				for i in range(nr.size()):
					nr[i] = (nb * nr[i]).normalized()
				arr[Mesh.ARRAY_NORMAL] = nr
			if arr[Mesh.ARRAY_TANGENT] != null:
				var tg: PackedFloat32Array = arr[Mesh.ARRAY_TANGENT]
				for i in range(0, tg.size(), 4):
					var t := (xf.basis * Vector3(tg[i], tg[i + 1], tg[i + 2])).normalized()
					tg[i] = t.x
					tg[i + 1] = t.y
					tg[i + 2] = t.z
				arr[Mesh.ARRAY_TANGENT] = tg
			arr[Mesh.ARRAY_BONES] = null
			arr[Mesh.ARRAY_WEIGHTS] = null
			out.add_surface_from_arrays(mesh.surface_get_primitive_type(s), arr)
			var mat: Material = mi.get_surface_override_material(s)
			if mat == null:
				mat = mesh.surface_get_material(s)
			out.surface_set_material(out.get_surface_count() - 1, mat)
	for c in node.get_children():
		_bake(c, xf, out)


# ---------- .obj (+ .mtl) 작은 해석기: v, vt, vn, f(다각형은 부채꼴로 나눔), usemtl, mtllib / newmtl, Kd, d, Tr, map_Kd ----------
static func _load_obj(path: String) -> ArrayMesh:
	var text := FileAccess.get_file_as_string(path)
	if text == "":
		return null
	var dir := path.get_base_dir()
	var pos := PackedVector3Array()
	var uvs := PackedVector2Array()
	var nrm := PackedVector3Array()
	var mats := {}
	var groups := {}          # 재질 이름 -> [[p, t, n] 꼭짓점 ...]
	var order := []
	var cur := ""
	for raw in text.split("\n"):
		var line := raw.strip_edges()
		if line == "" or line.begins_with("#"):
			continue
		var t := line.split(" ", false)
		match t[0]:
			"v":
				pos.append(Vector3(float(t[1]), float(t[2]), float(t[3])))
			"vt":
				uvs.append(Vector2(float(t[1]), 1.0 - float(t[2])))
			"vn":
				nrm.append(Vector3(float(t[1]), float(t[2]), float(t[3])))
			"mtllib":
				mats.merge(_load_mtl(dir.path_join(line.substr(7).strip_edges())))
			"usemtl":
				cur = line.substr(7).strip_edges()
			"f":
				if not groups.has(cur):
					groups[cur] = []
					order.append(cur)
				var vs := []
				for k in range(1, t.size()):
					var parts := t[k].split("/")
					var pi := _obj_index(parts[0], pos.size())
					var ti := _obj_index(parts[1], uvs.size()) if parts.size() > 1 and parts[1] != "" else -1
					var ni := _obj_index(parts[2], nrm.size()) if parts.size() > 2 and parts[2] != "" else -1
					vs.append([pi, ti, ni])
				for k in range(1, vs.size() - 1):
					groups[cur].append_array([vs[0], vs[k + 1], vs[k]])   # OBJ(반시계) → Godot 앞면(시계)
	var out := ArrayMesh.new()
	for g in order:
		var st := SurfaceTool.new()
		st.begin(Mesh.PRIMITIVE_TRIANGLES)
		var has_n := true
		for vtx in groups[g]:
			if vtx[2] < 0:
				has_n = false
		for vtx in groups[g]:
			if vtx[1] >= 0:
				st.set_uv(uvs[vtx[1]])
			if has_n:
				st.set_normal(nrm[vtx[2]])
			st.add_vertex(pos[vtx[0]])
		if not has_n:
			st.generate_normals()
		st.commit(out)
		var m: Material = mats.get(g, null)
		if m == null:
			var d := StandardMaterial3D.new()
			d.albedo_color = Color(0.8, 0.8, 0.8)
			m = d
		out.surface_set_material(out.get_surface_count() - 1, m)
	return out


static func _obj_index(s: String, count: int) -> int:
	var i := int(s)
	return count + i if i < 0 else i - 1


static func _load_mtl(path: String) -> Dictionary:
	var out := {}
	if not FileAccess.file_exists(path):
		return out
	var cur: StandardMaterial3D = null
	for raw in FileAccess.get_file_as_string(path).split("\n"):
		var line := raw.strip_edges()
		var t := line.split(" ", false)
		if t.is_empty():
			continue
		match t[0]:
			"newmtl":
				cur = StandardMaterial3D.new()
				cur.roughness = 0.85
				cur.cull_mode = BaseMaterial3D.CULL_DISABLED
				out[line.substr(7).strip_edges()] = cur
			"Kd":
				if cur:
					cur.albedo_color = Color(float(t[1]), float(t[2]), float(t[3]), cur.albedo_color.a)
			"d", "Tr":
				if cur:
					var a := float(t[1]) if t[0] == "d" else 1.0 - float(t[1])
					if a < 0.999:
						cur.albedo_color.a = a
						cur.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
			"map_Kd":
				if cur:
					var tex := load_image_texture(path.get_base_dir().path_join(t[t.size() - 1]))
					if tex:
						cur.albedo_texture = tex
	return out


static func load_image_texture(path: String) -> ImageTexture:
	if not FileAccess.file_exists(path):
		return null
	var img := Image.load_from_file(ProjectSettings.globalize_path(path))
	if img == null or img.is_empty():
		return null
	img.generate_mipmaps()
	return ImageTexture.create_from_image(img)


# ---------- 캐릭터 그림 (art/sprites/character_<front|back|side>.png, 없으면 그려 둔 SVG) ----------
# 돌려주는 사전: {texture, base_frac(그림 아래에서 발바닥까지 비율), placeholder}
static func character(dir: String, frame: int) -> Dictionary:
	var key := "char_%s_%d" % [dir, frame]
	if _cache.has(key):
		return _cache[key]
	var p := SPRITE_DIR.path_join("character_%s.png" % dir)
	var r := {}
	var tex := load_image_texture(p)
	if tex != null:
		r = {"texture": tex, "base_frac": 0.0, "placeholder": false}
	else:
		r = {"texture": Soft.character_texture(dir, frame), "base_frac": 0.02, "placeholder": true}
	_cache[key] = r
	return r


# 도구 줄 그림 (art/icons/tool_<id>.png, 128 × 128 권장, 없으면 그려 둔 SVG)
# px: 화면에 그려질 크기. 그려 둔 SVG는 그 크기로 다시 그려요(PNG 자리는 밉맵으로 줄여요)
static func tool_icon(tool_id: String, px := 128) -> Texture2D:
	var key := "icon_%s_%d" % [tool_id, px]
	if _cache.has(key):
		return _cache[key]
	var tex: Texture2D = load_image_texture(ICON_DIR.path_join("tool_%s.png" % tool_id))
	if tex == null:
		tex = Soft.tool_icon(tool_id, px)
	_cache[key] = tex
	return tex


# 자리마다 지금 무엇을 쓰는지 (README·보고용)
static func report() -> Array:
	var out := []
	for slot in PLANT_SLOTS + STRUCTURE_SLOTS + DEVICE_SLOTS:
		var m := model(slot)
		out.append({"slot": slot, "source": ("placeholder" if m.get("placeholder", true) else m.get("path", "")) if not m.is_empty() else "none", "tris": m.get("tris", 0)})
	for s in CHARACTER_SPRITES:
		out.append({"slot": s, "source": "png" if FileAccess.file_exists(SPRITE_DIR.path_join(s + ".png")) else "placeholder(svg)"})
	return out
