# 코드로 만드는 간단한 3D 모양 도구 (자리 채움 모델용). 부품을 정점 색과 함께 한 메시로 합쳐요.
# 1 단위 = 1 m, Y 위, 원점 = 땅에 닿는 가운데 (art/README.md 규약과 같아요).
extends RefCounted

var _v := PackedVector3Array()
var _c := PackedColorArray()


func tri_count() -> int:
	return _v.size() / 3


func add_tri(a: Vector3, b: Vector3, c: Vector3, ca: Color, cb: Color, cc: Color) -> void:
	_v.append_array([a, b, c])
	_c.append_array([ca, cb, cc])


# Godot 기본 모양(Box/Cylinder/Sphere/Torus/Capsule…)을 변환해서 한 색으로 붙여요
func add_primitive(m: PrimitiveMesh, xf: Transform3D, col: Color, col_top := Color(0, 0, 0, 0)) -> void:
	var arr := m.get_mesh_arrays()
	var verts: PackedVector3Array = arr[Mesh.ARRAY_VERTEX]
	var idx: PackedInt32Array = arr[Mesh.ARRAY_INDEX]
	var aabb := m.get_aabb()
	var grad := col_top.a > 0.0
	for i in range(0, idx.size(), 3):
		var tri := []
		var cols := []
		for k in range(3):
			var p: Vector3 = verts[idx[i + k]]
			tri.append(xf * p)
			if grad:
				var t := clampf((p.y - aabb.position.y) / maxf(0.0001, aabb.size.y), 0.0, 1.0)
				cols.append(col.lerp(col_top, t))
			else:
				cols.append(col)
		add_tri(tri[0], tri[1], tri[2], cols[0], cols[1], cols[2])


# 모서리가 둥근 상자 (초타원체). size = 전체 크기, round = 0(뾰족)~1(공), 바닥이 y = 0
func add_rounded_box(size: Vector3, roundness: float, xf: Transform3D, col: Color, col_top := Color(0, 0, 0, 0), seg := 10) -> void:
	var e := clampf(roundness, 0.05, 1.0)
	var rings := seg
	var pts := []
	for r in range(rings + 1):
		var th := PI * r / rings
		var row := []
		for s in range(seg * 2 + 1):
			var ph := TAU * s / (seg * 2)
			var d := Vector3(sin(th) * cos(ph), cos(th), sin(th) * sin(ph))
			var q := Vector3(_sp(d.x, e), _sp(d.y, e), _sp(d.z, e))
			row.append(Vector3(q.x * size.x * 0.5, (q.y + 1.0) * size.y * 0.5, q.z * size.z * 0.5))
		pts.append(row)
	var grad := col_top.a > 0.0
	for r in range(rings):
		for s in range(seg * 2):
			var a: Vector3 = pts[r][s]
			var b: Vector3 = pts[r][s + 1]
			var c: Vector3 = pts[r + 1][s]
			var d2: Vector3 = pts[r + 1][s + 1]
			var ca := col.lerp(col_top, a.y / size.y) if grad else col
			var cb := col.lerp(col_top, b.y / size.y) if grad else col
			var cc := col.lerp(col_top, c.y / size.y) if grad else col
			var cd := col.lerp(col_top, d2.y / size.y) if grad else col
			add_tri(xf * a, xf * b, xf * c, ca, cb, cc)
			add_tri(xf * b, xf * d2, xf * c, cb, cd, cc)


static func _sp(x: float, e: float) -> float:
	return signf(x) * pow(absf(x), e)


# 살짝 오목한 타원 잎 한 장 (밑동 → 끝 방향 +Z, 잎 면은 위쪽). n = 둘레 조각, rings = 동심 고리
func add_leaflet(length: float, width: float, cup: float, xf: Transform3D, c_base: Color, c_tip: Color, c_edge: Color, n := 10, rings := 2) -> void:
	var center := Vector3(0, cup * 0.5, length * 0.5)
	var prev := []
	for r in range(1, rings + 1):
		var f := float(r) / rings
		var ring := []
		for i in range(n):
			var a := TAU * i / n
			var x := sin(a) * width * 0.5 * f
			var z := length * 0.5 + cos(a) * length * 0.5 * f
			var y := cup * (1.0 - f * f) * 0.5 + cup * f * f * 0.9 * (absf(x) / maxf(0.0001, width * 0.5))
			ring.append(Vector3(x, y, z))
		for i in range(n):
			var j := (i + 1) % n
			var tcol := func(p: Vector3) -> Color:
				var t := clampf(p.z / length, 0.0, 1.0)
				var edge := clampf(Vector2(p.x / (width * 0.5), (p.z - length * 0.5) / (length * 0.5)).length(), 0.0, 1.0)
				return c_base.lerp(c_tip, t).lerp(c_edge, edge * edge * 0.6)
			if r == 1:
				add_tri(xf * center, xf * ring[i], xf * ring[j], tcol.call(center), tcol.call(ring[i]), tcol.call(ring[j]))
			else:
				add_tri(xf * prev[i], xf * ring[i], xf * ring[j], tcol.call(prev[i]), tcol.call(ring[i]), tcol.call(ring[j]))
				add_tri(xf * prev[i], xf * ring[j], xf * prev[j], tcol.call(prev[i]), tcol.call(ring[j]), tcol.call(prev[j]))
		prev = ring


func commit(mat: Material) -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	for i in range(_v.size()):
		st.set_color(_c[i])
		st.add_vertex(_v[i])
	st.index()
	st.generate_normals()
	var m := st.commit()
	m.surface_set_material(0, mat)
	return m


# 정점 색을 바탕색으로 쓰는 부드러운 재질 (빛을 감싸 듯 받아요)
static func soft_material(transparent := false, alpha := 1.0) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.vertex_color_use_as_albedo = true
	m.vertex_color_is_srgb = true   # 코드에서 넣은 색은 sRGB예요 (안 켜면 색이 허옇게 떠요)
	m.albedo_color = Color(1, 1, 1, alpha)
	m.roughness = 0.9
	m.specular_mode = BaseMaterial3D.SPECULAR_DISABLED
	m.diffuse_mode = BaseMaterial3D.DIFFUSE_LAMBERT_WRAP
	m.cull_mode = BaseMaterial3D.CULL_DISABLED
	if transparent:
		m.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		m.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	return m
