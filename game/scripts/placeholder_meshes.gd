# 자리 채움 3D 모델 (디렉터 모델이 art/models/에 없을 때 쓰는 코드 모델). 전부 임시 모양이에요.
# 규약: 1 단위 = 1 m, Y 위, 원점 = 땅(베드 윗면)에 닿는 가운데, 앞면 = +Z(카메라 쪽).
extends RefCounted

const MeshKit = preload("res://scripts/mesh_kit.gd")

const LEAF_BASE := Color("#3d7a3a")
const LEAF_TIP := Color("#8fd16a")
const LEAF_EDGE := Color("#4f9a44")
const STEM := Color("#6f9a45")

# 크기 단계 1~4: [잎 수, 줄기 길이 m, 작은 잎 길이 m]
const STAGES := [[3, 0.05, 0.045], [5, 0.075, 0.06], [6, 0.10, 0.07], [8, 0.12, 0.08]]


static func _xf(origin: Vector3, yaw := 0.0, pitch := 0.0, scale := Vector3.ONE) -> Transform3D:
	var b := Basis(Vector3.UP, yaw) * Basis(Vector3.RIGHT, pitch)
	return Transform3D(b.scaled_local(scale), origin)


# 딸기 포기 로제트. detail이 클수록 삼각형이 많아요(프레임 시간 측정용)
static func plant(stage: int, detail := 1, dead := false) -> Dictionary:
	var k := MeshKit.new()
	var sp: Array = STAGES[clampi(stage, 1, 4) - 1]
	var n_leaves: int = sp[0]
	var stem_len: float = sp[1]
	var leaf_len: float = sp[2]
	var segs := 8 + 4 * detail
	var rings := 1 + detail / 2
	var cb := LEAF_BASE
	var ct := LEAF_TIP
	var ce := LEAF_EDGE
	if dead:
		cb = Color("#7d6340")
		ct = Color("#c9b07a")
		ce = Color("#8a6d45")
	for i in range(n_leaves):
		var yaw := TAU * i / n_leaves + 0.35 * sin(i * 2.1)
		var lift := deg_to_rad(55.0 - 8.0 * (i % 3)) if not dead else deg_to_rad(-10.0)
		var dir := Vector3(sin(yaw) * cos(lift), sin(lift), cos(yaw) * cos(lift))
		var tip := dir * stem_len
		# 줄기
		var cyl := CylinderMesh.new()
		cyl.top_radius = 0.004
		cyl.bottom_radius = 0.005
		cyl.height = stem_len
		cyl.radial_segments = 5
		cyl.rings = 1
		var mid := tip * 0.5
		var up := dir.normalized()
		var basis := _basis_y_to(up)
		k.add_primitive(cyl, Transform3D(basis, mid), STEM)
		# 작은 잎 세 장 (가운데 + 양옆)
		for a in [-0.75, 0.0, 0.75]:
			var pitch := deg_to_rad(-28.0) if not dead else deg_to_rad(40.0)   # 음수 = 위로 들림, 양수 = 처짐
			var sc := 1.0 if a == 0.0 else 0.85
			var xf := Transform3D(Basis(Vector3.UP, yaw + a) * Basis(Vector3.RIGHT, pitch), tip)
			k.add_leaflet(leaf_len * sc, leaf_len * 0.62 * sc, leaf_len * 0.18, xf, cb, ct, ce, segs, rings)
	# 가운데 새순
	var crown := SphereMesh.new()
	crown.radius = 0.012
	crown.height = 0.02
	crown.radial_segments = 6
	crown.rings = 3
	k.add_primitive(crown, _xf(Vector3(0, 0.005, 0)), cb.darkened(0.1))
	return {"mesh": k.commit(MeshKit.soft_material()), "tris": k.tri_count()}


static func _basis_y_to(dir: Vector3) -> Basis:
	var y := dir.normalized()
	var x := y.cross(Vector3.FORWARD if absf(y.z) < 0.9 else Vector3.RIGHT).normalized()
	var z := x.cross(y).normalized()
	return Basis(x, y, z)


static func flower() -> Dictionary:
	var k := MeshKit.new()
	for i in range(5):
		var a := TAU * i / 5.0
		var petal := SphereMesh.new()
		petal.radius = 0.011
		petal.height = 0.008
		petal.radial_segments = 8
		petal.rings = 3
		k.add_primitive(petal, Transform3D(Basis.IDENTITY, Vector3(sin(a) * 0.012, 0.0, cos(a) * 0.012)), Color("#fffaf0"))
	var c := SphereMesh.new()
	c.radius = 0.008
	c.height = 0.008
	c.radial_segments = 8
	c.rings = 3
	k.add_primitive(c, Transform3D(Basis.IDENTITY, Vector3(0, 0.003, 0)), Color("#f6c84a"))
	var stalk := CylinderMesh.new()
	stalk.top_radius = 0.002
	stalk.bottom_radius = 0.002
	stalk.height = 0.05
	stalk.radial_segments = 4
	stalk.rings = 1
	k.add_primitive(stalk, Transform3D(Basis.IDENTITY, Vector3(0, -0.025, 0)), STEM)
	return {"mesh": k.commit(MeshKit.soft_material()), "tris": k.tri_count()}


# 딸기 열매: 원뿔 비슷한 회전체 + 꽃받침 (실제보다 조금 크게: 길이 약 5 cm)
static func fruit(ripe: bool) -> Dictionary:
	var k := MeshKit.new()
	var c0 := Color("#e5384a") if ripe else Color("#b9d46b")
	var c1 := Color("#ff8a7a") if ripe else Color("#eaf6b8")
	var prof := [Vector2(0.0, 0.0), Vector2(0.008, 0.004), Vector2(0.017, 0.016), Vector2(0.021, 0.03), Vector2(0.018, 0.042), Vector2(0.008, 0.049), Vector2(0.0, 0.05)]
	var n := 12
	for r in range(prof.size() - 1):
		var p0: Vector2 = prof[r]
		var p1: Vector2 = prof[r + 1]
		for i in range(n):
			var a0 := TAU * i / n
			var a1 := TAU * (i + 1) / n
			var v00 := Vector3(sin(a0) * p0.x, p0.y, cos(a0) * p0.x)
			var v01 := Vector3(sin(a1) * p0.x, p0.y, cos(a1) * p0.x)
			var v10 := Vector3(sin(a0) * p1.x, p1.y, cos(a0) * p1.x)
			var v11 := Vector3(sin(a1) * p1.x, p1.y, cos(a1) * p1.x)
			var t0 := c0.lerp(c1, p0.y / 0.05 * 0.6)
			var t1 := c0.lerp(c1, p1.y / 0.05 * 0.6)
			k.add_tri(v00, v10, v01, t0, t1, t0)
			k.add_tri(v01, v10, v11, t0, t1, t1)
	for i in range(5):
		var a := TAU * i / 5.0
		var leaf := SphereMesh.new()
		leaf.radius = 0.008
		leaf.height = 0.004
		leaf.radial_segments = 6
		leaf.rings = 2
		k.add_primitive(leaf, Transform3D(Basis.IDENTITY, Vector3(sin(a) * 0.009, 0.05, cos(a) * 0.009)), Color("#5fae4e"))
	return {"mesh": k.commit(MeshKit.soft_material()), "tris": k.tri_count()}


# 베드 한 칸 (1 m × 1 m, 높이 = bed_h). 흙 무늬는 재질에서 (삼평면 투영)
static func bed(bed_h: float, soil_tex: Texture2D) -> Dictionary:
	var k := MeshKit.new()
	k.add_rounded_box(Vector3(1.0, bed_h, 1.0), 0.07, Transform3D.IDENTITY, Color("#a07a58"), Color("#ffffff"), 8)   # 거의 모난 상자: 이웃 칸과 이어 보이게
	var m := MeshKit.soft_material()
	m.albedo_texture = soil_tex
	m.uv1_triplanar = true
	m.uv1_scale = Vector3(0.7, 0.7, 0.7)
	m.texture_filter = BaseMaterial3D.TEXTURE_FILTER_LINEAR_WITH_MIPMAPS_ANISOTROPIC
	return {"mesh": k.commit(m), "tris": k.tri_count()}


# 멀칭 한 칸 (1 m × 1 m, 베드 윗면에 덮는 얇은 필름, 원점 = 베드 윗면 가운데)
static func mulch(kind: String) -> Dictionary:
	var k := MeshKit.new()
	var c := Color("#2e3034")
	var top := Color("#4a4e55")
	var alpha := 1.0
	match kind:
		"white_film":
			c = Color("#e9e4d6")
			top = Color("#fffdf6")
		"clear_film":
			c = Color("#dff1fb")
			top = Color("#ffffff")
			alpha = 0.38
	k.add_rounded_box(Vector3(1.0, 0.012, 1.0), 0.12, Transform3D(Basis.IDENTITY, Vector3(0, -0.004, 0)), c, top, 6)
	return {"mesh": k.commit(MeshKit.soft_material(alpha < 1.0, alpha)), "tris": k.tri_count()}


# 비닐하우스 뼈대 1 m 구간 (기준 폭 width_m, 원점 = 바닥 가운데). P7: 비닐은 피복 겹 상태대로 house_cover.gd가 따로 그려요
static func house_frame(width_m: float) -> Dictionary:
	var ribs := MeshKit.new()
	var wall_h := 1.0
	var R := width_m * 0.5
	var max_a := deg_to_rad(70.0)
	var prof := [Vector2(-R, 0), Vector2(-R, wall_h)]
	for i in range(1, 13):
		var a := PI - max_a * i / 12.0
		prof.append(Vector2(R * cos(a), wall_h + R * sin(a)))
	var rc := Color(0.86, 0.88, 0.9)
	var rc_top := Color(0.97, 0.98, 0.99)
	for side in [-1.0, 1.0]:
		for i in range(prof.size() - 1):
			var p0: Vector2 = prof[i]
			var p1: Vector2 = prof[i + 1]
			var q0 := Vector2(p0.x * -side, p0.y)
			var q1 := Vector2(p1.x * -side, p1.y)
			# 둥근 관 (짧은 원기둥 조각)
			var d := Vector3(q1.x - q0.x, q1.y - q0.y, 0)
			var cyl := CylinderMesh.new()
			cyl.top_radius = 0.017
			cyl.bottom_radius = 0.017
			cyl.height = d.length() + 0.01
			cyl.radial_segments = 6
			cyl.rings = 1
			var y := d.normalized()
			var x := y.cross(Vector3.BACK).normalized()
			var basis := Basis(x, y, x.cross(y))
			var mid := Vector3((q0.x + q1.x) * 0.5, (q0.y + q1.y) * 0.5, -0.5)
			ribs.add_primitive(cyl, Transform3D(basis, mid), rc.lerp(rc_top, mid.y / 4.0))
	return {"mesh": ribs.commit(MeshKit.soft_material()), "tris": ribs.tri_count()}


# ---------- 장치 (원점 = 바닥 가운데, 앞면 +Z) ----------
static func device(slot: String) -> Dictionary:
	var k := MeshKit.new()
	var I := Transform3D.IDENTITY
	match slot:
		"heater":
			k.add_rounded_box(Vector3(0.62, 0.62, 0.36), 0.25, _xf(Vector3(0, 0.06, 0)), Color("#c9533a"), Color("#f08a5d"))
			for i in range(5):
				k.add_rounded_box(Vector3(0.44, 0.03, 0.02), 0.4, _xf(Vector3(0, 0.2 + 0.07 * i, 0.18)), Color("#8c3526"))
			var dial := CylinderMesh.new()
			dial.top_radius = 0.04
			dial.bottom_radius = 0.04
			dial.height = 0.03
			dial.radial_segments = 12
			k.add_primitive(dial, _xf(Vector3(-0.2, 0.58, 0.18), 0.0, PI * 0.5), Color("#eef1f3"))
			for x in [-0.22, 0.22]:
				k.add_rounded_box(Vector3(0.1, 0.06, 0.3), 0.4, _xf(Vector3(x, 0, 0)), Color("#6b4a32"))
		"fan_switch":
			k.add_rounded_box(Vector3(0.06, 1.2, 0.06), 0.5, I, Color("#b9c2c9"), Color("#eef1f3"))
			var ring := TorusMesh.new()
			ring.inner_radius = 0.2
			ring.outer_radius = 0.24
			ring.rings = 24
			ring.ring_segments = 8
			k.add_primitive(ring, _xf(Vector3(0, 1.2, 0.04), 0.0, PI * 0.5), Color("#dfe8ee"))
			for i in range(4):
				var a := TAU * i / 4.0 + 0.4
				var blade := SphereMesh.new()
				blade.radius = 0.09
				blade.height = 0.02
				blade.radial_segments = 10
				blade.rings = 3
				k.add_primitive(blade, Transform3D(Basis(Vector3.BACK, a) * Basis(Vector3.RIGHT, PI * 0.5).scaled(Vector3(0.6, 1, 1.2)), Vector3(cos(a) * 0.1, 1.2 + sin(a) * 0.1, 0.05)), Color("#8fc3e6"))
			k.add_rounded_box(Vector3(0.16, 0.2, 0.06), 0.3, _xf(Vector3(0.0, 0.7, 0.05)), Color("#f3f0e6"))
			k.add_rounded_box(Vector3(0.05, 0.09, 0.04), 0.4, _xf(Vector3(0.0, 0.75, 0.09)), Color("#5fae4e"))
		"window_panel":
			# 측창 조작판: 두 기둥 위 판, 겹(줄) × 좌우(칸) 손잡이 8개, 위에 작은 아치 그림
			for x in [-0.24, 0.24]:
				k.add_rounded_box(Vector3(0.05, 1.0, 0.05), 0.5, _xf(Vector3(x, 0, -0.04)), Color("#9aa6ad"), Color("#d6dde2"))
			k.add_rounded_box(Vector3(0.62, 0.62, 0.07), 0.22, _xf(Vector3(0, 0.78, 0)), Color("#3f6f9e"), Color("#6fa0d0"))
			k.add_rounded_box(Vector3(0.54, 0.5, 0.02), 0.2, _xf(Vector3(0, 0.82, 0.035)), Color("#e9eef2"), Color("#ffffff"))
			for row in range(4):
				for col in range(2):
					var px := -0.12 + 0.24 * col
					var py := 0.9 + 0.1 * row
					k.add_rounded_box(Vector3(0.16, 0.022, 0.02), 0.5, _xf(Vector3(px, py, 0.05)), Color("#b9c2c9"))
					var knob := SphereMesh.new()
					knob.radius = 0.022
					knob.height = 0.044
					knob.radial_segments = 10
					knob.rings = 5
					k.add_primitive(knob, _xf(Vector3(px - 0.05 + 0.03 * row, py + 0.011, 0.065)), Color("#d94a3c") if col == 0 else Color("#4a7fb8"))
			var arch := TorusMesh.new()
			arch.inner_radius = 0.13
			arch.outer_radius = 0.15
			arch.rings = 20
			arch.ring_segments = 6
			k.add_primitive(arch, _xf(Vector3(0, 1.32, 0.04), 0.0, PI * 0.5).scaled_local(Vector3(1, 1, 0.6)), Color("#f6c84a"))
		"workbench_seedlings":
			# 모종 작업대: 낮고 넓은 나무 탁자 + 검은 모종판 두 개에 모종이 빽빽이 (윗면이 초록)
			k.add_rounded_box(Vector3(0.72, 0.06, 0.42), 0.3, _xf(Vector3(0, 0.62, 0)), Color("#a8743f"), Color("#d8a86a"))
			for x in [-0.3, 0.3]:
				for z in [-0.16, 0.16]:
					k.add_rounded_box(Vector3(0.05, 0.62, 0.05), 0.5, _xf(Vector3(x, 0, z)), Color("#8a5e34"))
			k.add_rounded_box(Vector3(0.6, 0.03, 0.3), 0.4, _xf(Vector3(0, 0.2, 0)), Color("#8a5e34"))
			# 아래 선반에 빈 모종판 더미
			for j in range(3):
				k.add_rounded_box(Vector3(0.28, 0.025, 0.24), 0.3, _xf(Vector3(0.12, 0.23 + 0.027 * j, 0)), Color("#3b3f44"))
			for t in [-0.17, 0.17]:
				k.add_rounded_box(Vector3(0.32, 0.06, 0.34), 0.25, _xf(Vector3(t, 0.68, 0)), Color("#2f3236"))
				for i in range(9):
					var sx: float = t - 0.1 + 0.1 * (i % 3)
					var sz := -0.1 + 0.1 * (i / 3)
					var stem := CylinderMesh.new()
					stem.top_radius = 0.004
					stem.bottom_radius = 0.004
					stem.height = 0.06
					stem.radial_segments = 4
					stem.rings = 1
					k.add_primitive(stem, _xf(Vector3(sx, 0.77, sz)), Color("#5f9a44"))
					for lr in [-1.0, 1.0]:
						var leaf := SphereMesh.new()
						leaf.radius = 0.03
						leaf.height = 0.014
						leaf.radial_segments = 8
						leaf.rings = 3
						k.add_primitive(leaf, Transform3D(Basis(Vector3.BACK, lr * 0.4), Vector3(sx + lr * 0.025, 0.81, sz)), Color("#5fae4e"), Color("#a6dc7a"))
		"workbench_cover":
			# 피복 작업대: 키 큰 A자 거치대에 비닐 롤 세 개를 가로로 걸고, 한 롤은 비닐을 조금 풀어 늘어뜨려요
			for x in [-0.32, 0.32]:
				for z in [-0.16, 0.16]:
					var leg := Transform3D(Basis(Vector3.RIGHT, -signf(z) * 0.28), Vector3(x, 0.0, z))
					k.add_rounded_box(Vector3(0.045, 1.15, 0.045), 0.5, leg, Color("#7a8a96"), Color("#b9c2c9"))
				k.add_rounded_box(Vector3(0.05, 0.05, 0.36), 0.5, _xf(Vector3(x, 0.5, 0)), Color("#7a8a96"))
			var roll_y := [0.48, 0.78, 1.06]
			var roll_c := [Color("#dff1fb"), Color("#f4f1e6"), Color("#3a3d42")]   # 투명·백색·흑색 필름 롤
			for j in range(3):
				var cyl := CylinderMesh.new()
				cyl.top_radius = 0.075
				cyl.bottom_radius = 0.075
				cyl.height = 0.6
				cyl.radial_segments = 16
				cyl.rings = 1
				k.add_primitive(cyl, Transform3D(Basis(Vector3.BACK, PI * 0.5), Vector3(0, roll_y[j], 0.02 * j)), roll_c[j], roll_c[j].lightened(0.25))
				var core := CylinderMesh.new()
				core.top_radius = 0.02
				core.bottom_radius = 0.02
				core.height = 0.7
				core.radial_segments = 8
				core.rings = 1
				k.add_primitive(core, Transform3D(Basis(Vector3.BACK, PI * 0.5), Vector3(0, roll_y[j], 0.02 * j)), Color("#a8743f"))
			# 풀어 늘어뜨린 비닐 한 장 (가운데 롤에서 앞으로)
			k.add_rounded_box(Vector3(0.56, 0.62, 0.012), 0.15, Transform3D(Basis(Vector3.RIGHT, 0.12), Vector3(0, 0.16, 0.1)), Color("#e8e4d6"), Color("#fbf9f2"))
		"ceiling_fan":
			# 천장에 매단 유동팬 몸통 (원점 = 팬 가운데, 바람 방향 +Z): 둥근 망 테 + 모터 + 위로 매다는 막대
			var ring := TorusMesh.new()
			ring.inner_radius = 0.26
			ring.outer_radius = 0.29
			ring.rings = 28
			ring.ring_segments = 8
			k.add_primitive(ring, Transform3D(Basis(Vector3.RIGHT, PI * 0.5), Vector3(0, 0, 0.04)), Color("#c9d3da"))
			k.add_primitive(ring, Transform3D(Basis(Vector3.RIGHT, PI * 0.5), Vector3(0, 0, -0.04)), Color("#b5c0c8"))
			for i in range(6):
				var a := TAU * i / 6.0
				k.add_rounded_box(Vector3(0.012, 0.27, 0.012), 0.5, Transform3D(Basis(Vector3.BACK, a), Vector3(0, 0, 0.05)).translated_local(Vector3(0, 0.0, 0)), Color("#d6dde2"))
			var motor := CylinderMesh.new()
			motor.top_radius = 0.07
			motor.bottom_radius = 0.08
			motor.height = 0.16
			motor.radial_segments = 14
			motor.rings = 1
			k.add_primitive(motor, Transform3D(Basis(Vector3.RIGHT, PI * 0.5), Vector3(0, 0, -0.1)), Color("#7d8a94"), Color("#aab4bc"))
			k.add_rounded_box(Vector3(0.03, 0.9, 0.03), 0.5, _xf(Vector3(0, 0.25, -0.05)), Color("#9aa6ad"))
			k.add_rounded_box(Vector3(0.22, 0.03, 0.06), 0.5, _xf(Vector3(0, 0.27, -0.05)), Color("#8a959c"))
		"ceiling_fan_blades":
			# 도는 날개 (원점 = 축, 축 = Z): 날개 4장 + 가운데 뚜껑
			for i in range(4):
				var a := TAU * i / 4.0
				var blade := SphereMesh.new()
				blade.radius = 0.12
				blade.height = 0.03
				blade.radial_segments = 12
				blade.rings = 4
				k.add_primitive(blade, Transform3D(Basis(Vector3.BACK, a) * Basis(Vector3.UP, 0.35) * Basis(Vector3.RIGHT, PI * 0.5).scaled(Vector3(0.55, 1, 1.0)), Vector3(cos(a) * 0.13, sin(a) * 0.13, 0.0)), Color("#7fb2e0"), Color("#a6cdef"))
			var cap := SphereMesh.new()
			cap.radius = 0.04
			cap.height = 0.05
			cap.radial_segments = 10
			cap.rings = 5
			k.add_primitive(cap, Transform3D(Basis.IDENTITY, Vector3(0, 0, 0.01)), Color("#e8eef2"))
		"door":
			k.add_rounded_box(Vector3(0.9, 2.0, 0.08), 0.15, I, Color("#a8743f"), Color("#d8a86a"))
			k.add_rounded_box(Vector3(0.7, 1.8, 0.04), 0.1, _xf(Vector3(0, 0.1, 0.03)), Color("#bfdced"), Color("#eaf6ff"))
			k.add_rounded_box(Vector3(0.7, 0.05, 0.05), 0.4, _xf(Vector3(0, 0.95, 0.05)), Color("#a8743f"))
			var h := SphereMesh.new()
			h.radius = 0.035
			h.height = 0.07
			h.radial_segments = 12
			h.rings = 6
			k.add_primitive(h, _xf(Vector3(0.3, 1.0, 0.08)), Color("#f2cf6a"))
		"drip_valve":
			k.add_rounded_box(Vector3(0.1, 0.9, 0.1), 0.5, I, Color("#4a7fb8"), Color("#7fb2e0"))
			var wheel := TorusMesh.new()
			wheel.inner_radius = 0.1
			wheel.outer_radius = 0.135
			wheel.rings = 20
			wheel.ring_segments = 8
			k.add_primitive(wheel, _xf(Vector3(0, 0.78, 0.08), 0.0, PI * 0.5), Color("#d94a3c"))
			k.add_rounded_box(Vector3(0.24, 0.03, 0.03), 0.5, _xf(Vector3(0, 0.77, 0.08)), Color("#b53a2e"))
			var hose := TorusMesh.new()
			hose.inner_radius = 0.14
			hose.outer_radius = 0.17
			hose.rings = 20
			hose.ring_segments = 6
			k.add_primitive(hose, _xf(Vector3(0.18, 0.02, 0.1)), Color("#34383d"))
		"irrigation_controller":
			k.add_rounded_box(Vector3(0.07, 1.0, 0.07), 0.5, I, Color("#b9c2c9"), Color("#eef1f3"))
			k.add_rounded_box(Vector3(0.5, 0.38, 0.14), 0.25, _xf(Vector3(0, 0.9, 0.03)), Color("#4a9990"), Color("#7cc7c0"))
			k.add_rounded_box(Vector3(0.26, 0.14, 0.02), 0.2, _xf(Vector3(-0.06, 1.06, 0.1)), Color("#dff5d8"))
			var b1 := SphereMesh.new()
			b1.radius = 0.025
			b1.height = 0.03
			b1.radial_segments = 12
			b1.rings = 6
			k.add_primitive(b1, _xf(Vector3(0.16, 1.12, 0.1)), Color("#f6c84a"))
			k.add_primitive(b1, _xf(Vector3(0.16, 1.02, 0.1)), Color("#f3f0e6"))
	return {"mesh": k.commit(MeshKit.soft_material()), "tris": k.tri_count()}
