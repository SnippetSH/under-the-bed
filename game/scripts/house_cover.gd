# 피복 비닐 겹과 측창 (M6b P7). 보이는 것 get_house_controls()의 cover_layers, window_open_fraction만 읽어요.
# 겹 0 = 가장 바깥. 겹마다 옆벽 아래쪽에 좌·우 측창(0행 쪽 벽 = 좌)이 따로 있고, 열린 정도만큼 비닐이 말려 올라가요.
# 시야: 아치 윗부분은 비워 두고(arch_cut_deg), 비닐은 반투명이라 베드·캐릭터를 가리지 않아요. 카메라 쪽 끝 벽은 없어요.
extends Node3D

const Cfg = preload("res://scripts/cfg.gd")
const MeshKit = preload("res://scripts/mesh_kit.gd")

var W := 6.7
var z0 := -2.2
var z1 := 20.9
var wall_h := 1.0
var sill := 0.12
var spacing := 0.08
var cut := deg_to_rad(58.0)
var anim_s := 0.6
var alpha := 0.13
var win_alpha := 0.34
var layers: Array = []           # 겹마다 {root, win: [좌, 우 Node3D], roll: [좌, 우], anim: [{from, to, t}, …]}
var _unit_film: ArrayMesh        # 높이 1, 길이 z0~z1의 세로 비닐 (x = 0 평면)
var _n := -1


func setup(layout: Object) -> void:
	W = layout.width_m
	z0 = -layout.front_m + 0.05
	z1 = layout.length_m + layout.back_m
	wall_h = float(Cfg.get_v("cover", "wall_height_m"))
	sill = float(Cfg.get_v("cover", "window_sill_m"))
	spacing = float(Cfg.get_v("cover", "layer_spacing_m"))
	cut = deg_to_rad(float(Cfg.get_v("cover", "arch_cut_deg")))
	anim_s = float(Cfg.get_v("cover", "window_anim_seconds"))
	alpha = float(Cfg.get_v("cover", "film_alpha"))
	_unit_film = _quad_strip([Vector2(0, 0), Vector2(0, 1)])
	win_alpha = float(Cfg.get_v("cover", "window_film_alpha"))
	# P18: 옆벽 바깥 색 띠는 없앴어요. 열린 틈으로 진짜 바깥 땅(house_view_3d의 큰 평면)이 보여요


const LAYER_COLORS := [Color("#7fb2e0"), Color("#86c9a0"), Color("#f0c27a"), Color("#e79ab0")]   # 겹 0(바깥)~3 구분 색


func _tint(k: int, _n: int) -> Color:
	# 비닐은 겹 색을 아주 옅게 섞은 흰색 (겹을 구분해 보이게)
	return Color(0.95, 0.97, 1.0).lerp(LAYER_COLORS[k % LAYER_COLORS.size()], 0.35)


func _film_mat(c: Color, a: float) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = Color(c.r, c.g, c.b, a)
	m.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	m.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	m.cull_mode = BaseMaterial3D.CULL_DISABLED
	m.render_priority = -1
	return m


# x-y 단면 곡선을 z0~z1로 늘인 면
func _quad_strip(prof: Array) -> ArrayMesh:
	var v := PackedVector3Array()
	for i in range(prof.size() - 1):
		var p0: Vector2 = prof[i]
		var p1: Vector2 = prof[i + 1]
		var a0 := Vector3(p0.x, p0.y, z0)
		var a1 := Vector3(p1.x, p1.y, z0)
		var b0 := Vector3(p0.x, p0.y, z1)
		var b1 := Vector3(p1.x, p1.y, z1)
		v.append_array([a0, b0, a1, a1, b0, b1])
	var am := ArrayMesh.new()
	var arr := []
	arr.resize(Mesh.ARRAY_MAX)
	arr[Mesh.ARRAY_VERTEX] = v
	am.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arr)
	return am


func _mi(mesh: Mesh, mat: Material, parent: Node) -> MeshInstance3D:
	var mi := MeshInstance3D.new()
	mi.mesh = mesh
	mi.material_override = mat
	mi.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	parent.add_child(mi)
	return mi


func _rebuild(n: int) -> void:
	for l in layers:
		l["root"].queue_free()
	layers.clear()
	_n = n
	for k in range(n):
		var inset := spacing * k
		var root := Node3D.new()
		add_child(root)
		var c := _tint(k, n)
		var film := _film_mat(c, alpha)
		var win_mat := _film_mat(c, win_alpha)   # 창 비닐은 덜 투명하게: 말려 올라간 틈으로 바깥 색이 또렷하게 보여요
		# 아치 (옆벽 위 ~ 잘라 둔 각도까지), 겹마다 반지름을 조금씩 줄여요
		var R := W * 0.5 - inset
		var prof := [Vector2(inset, wall_h)]
		for i in range(1, 11):
			var a := PI - cut * i / 10.0
			prof.append(Vector2(W * 0.5 + R * cos(a), wall_h + R * sin(a) * (1.0 - 0.02 * k)))
		var arch_l := _quad_strip(prof)
		var prof_r := prof.map(func(p): return Vector2(W - p.x, p.y))
		var arch_r := _quad_strip(prof_r)
		_mi(arch_l, film, root)
		_mi(arch_r, film, root)
		# 겹 경계를 보이게: 옆벽 위쪽 띠(창틀) — 겹 색으로 조금 진하게
		var bar := MeshKit.new()
		for x in [inset, W - inset]:
			bar.add_rounded_box(Vector3(0.03, 0.035, z1 - z0), 0.6, Transform3D(Basis.IDENTITY, Vector3(x, wall_h - 0.017, (z0 + z1) * 0.5)), LAYER_COLORS[k % LAYER_COLORS.size()].darkened(0.1))
			bar.add_rounded_box(Vector3(0.03, 0.03, z1 - z0), 0.6, Transform3D(Basis.IDENTITY, Vector3(x, sill - 0.015, (z0 + z1) * 0.5)), LAYER_COLORS[k % LAYER_COLORS.size()].darkened(0.25))
		_mi(bar.commit(MeshKit.soft_material()), null, root)
		# 측창 아래 고정 비닐 (0 ~ sill)
		for x in [inset, W - inset]:
			var sk := _mi(_unit_film, film, root)
			sk.position = Vector3(x, 0, 0)
			sk.scale = Vector3(1, sill, 1)
		# 측창: 좌·우 (말려 올라가는 비닐 + 말린 비닐 통)
		var wins := []
		var rolls := []
		var anims := []
		for side in [0, 1]:
			var x := inset if side == 0 else W - inset
			var wmi := _mi(_unit_film, win_mat, root)
			wmi.position = Vector3(x, sill, 0)
			wins.append(wmi)
			var cyl := CylinderMesh.new()
			cyl.top_radius = 1.0
			cyl.bottom_radius = 1.0
			cyl.height = z1 - z0
			cyl.radial_segments = 10
			cyl.rings = 1
			var rk := MeshKit.new()
			var lc: Color = LAYER_COLORS[k % LAYER_COLORS.size()]
			rk.add_primitive(cyl, Transform3D(Basis(Vector3.RIGHT, PI * 0.5), Vector3.ZERO), lc.darkened(0.15), lc.lightened(0.3))
			var rmi := _mi(rk.commit(MeshKit.soft_material()), null, root)
			rmi.position = Vector3(x, sill, (z0 + z1) * 0.5)
			rolls.append(rmi)
			anims.append({"from": -1.0, "to": -1.0, "t": 0.0, "shown": 0.0})
		layers.append({"root": root, "win": wins, "roll": rolls, "anim": anims})


# 매 프레임: 겹 수가 바뀌면 다시 만들고, 측창은 목표 값으로 부드럽게 움직여요
func sync(hc: Dictionary, delta: float) -> void:
	var n := int(hc["cover_layers"])
	if n != _n:
		_rebuild(n)
	var wf: PackedFloat64Array = hc["window_open_fraction"]
	for k in range(layers.size()):
		for side in [0, 1]:
			var i: int = k * 2 + side
			var target := float(wf[i]) if i < wf.size() else 0.0
			var a: Dictionary = layers[k]["anim"][side]
			if a["to"] < 0.0:
				a["from"] = target
				a["to"] = target
				a["t"] = anim_s
			elif not is_equal_approx(target, a["to"]):
				a["from"] = a["shown"]
				a["to"] = target
				a["t"] = 0.0
			a["t"] = minf(anim_s, a["t"] + delta)
			var f := lerpf(a["from"], a["to"], smoothstep(0.0, 1.0, a["t"] / maxf(0.001, anim_s)))
			a["shown"] = f
			_apply(k, side, f)


func _apply(k: int, side: int, f: float) -> void:
	var h := wall_h - sill
	var top := sill + f * h                      # 비닐이 말려 올라간 높이 (열린 부분 = sill ~ top)
	var win: MeshInstance3D = layers[k]["win"][side]
	win.position.y = top
	win.scale = Vector3(1, maxf(0.001, wall_h - top), 1)
	win.visible = wall_h - top > 0.002
	var roll: MeshInstance3D = layers[k]["roll"][side]
	var r := 0.012 + 0.03 * f                    # 많이 말릴수록 굵어요
	roll.position.y = top
	roll.scale = Vector3(r, r, 1)   # 반지름(x·y)만 줄이고 길이(z, 벽 전체)는 그대로 (P15 버그 고침)
	# 통이 벽 안쪽으로 살짝 나오게
	var x0: float = spacing * k if side == 0 else W - spacing * k
	roll.position.x = x0 + (r if side == 0 else -r)


# 검사·보고용: 지금 그려지는 열린 정도
func shown(k: int, side: int) -> float:
	return float(layers[k]["anim"][side]["shown"]) if k < layers.size() else -1.0
