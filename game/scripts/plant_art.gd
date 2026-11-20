# 임시 그림 (M6b): 포기·캐릭터·표시를 코드로 그린 작은 이미지. 내려받은 그림은 없어요.
# 칸 상태 → 그림 열쇠: "e"(빈 칸), "d"(죽은 포기), "p<단계><꽃><과실><익은 과실>".
extends RefCounted

const Cfg = preload("res://scripts/cfg.gd")

const PLANT_PX := 64
static var _cache := {}
static var _stage_thresholds: Array = []


static func stage_of(leaf_g: float) -> int:
	if _stage_thresholds.is_empty():
		_stage_thresholds = Cfg.get_v("visual", "leaf_stage_thresholds_g_per_m2")
	var s := 0
	for t in _stage_thresholds:
		if leaf_g >= float(t):
			s += 1
	return s


# 보이는 것(get_tile_visuals)만으로 칸 i의 그림 열쇠
static func key_for(v: Dictionary, i: int) -> String:
	if v["crop_present"][i] == 0:
		return "e"
	if v["crop_alive"][i] == 0:
		return "d"
	return "p%d%d%d%d" % [stage_of(v["leaf_g_per_m2"][i]), 1 if v["open_flower_count"][i] > 0 else 0,
			1 if v["fruit_count"][i] > 0 else 0, 1 if v["ripening_fruit_count"][i] > 0 else 0]


static func _disc(img: Image, c: Vector2, r: float, col: Color) -> void:
	var x0 := maxi(0, int(c.x - r))
	var x1 := mini(img.get_width() - 1, int(c.x + r))
	var y0 := maxi(0, int(c.y - r))
	var y1 := mini(img.get_height() - 1, int(c.y + r))
	for y in range(y0, y1 + 1):
		for x in range(x0, x1 + 1):
			if Vector2(x + 0.5, y + 0.5).distance_to(c) <= r:
				img.set_pixel(x, y, col)


static func _rect(img: Image, r: Rect2i, col: Color) -> void:
	img.fill_rect(r.intersection(Rect2i(0, 0, img.get_width(), img.get_height())), col)


static func texture(key: String) -> ImageTexture:
	if _cache.has(key):
		return _cache[key]
	var img := Image.create(PLANT_PX, PLANT_PX, false, Image.FORMAT_RGBA8)
	img.fill(Color(0, 0, 0, 0))
	var base := Vector2(32, 63)   # 그림 아래 가운데가 땅에 닿는 점
	if key == "e":
		pass
	elif key == "d":
		var dc := Cfg.color("dead_plant")
		for k in range(5):
			_disc(img, base + Vector2(-14 + 7 * k, -5 - (k % 2) * 3), 5.0, dc)
		_rect(img, Rect2i(30, 44, 3, 16), dc.darkened(0.3))
	else:
		var stage := int(key.substr(1, 1))
		var flower := key.substr(2, 1) == "1"
		var fruit := key.substr(3, 1) == "1"
		var ripe := key.substr(4, 1) == "1"
		var leaf := Cfg.color("plant_leaf")
		var dark := Cfg.color("plant_leaf_dark")
		var r := 8.0 + 4.5 * stage
		var cy := 63.0 - r * 0.9
		# 잎 덩어리: 겹친 원 몇 개 (위가 밝게)
		_disc(img, Vector2(32 - r * 0.55, cy + 2), r * 0.62, dark)
		_disc(img, Vector2(32 + r * 0.55, cy + 2), r * 0.62, dark)
		_disc(img, Vector2(32, cy), r * 0.7, leaf)
		_disc(img, Vector2(32 - r * 0.3, cy - r * 0.35), r * 0.45, leaf.lightened(0.15))
		_disc(img, Vector2(32 + r * 0.35, cy - r * 0.25), r * 0.4, leaf.lightened(0.08))
		if flower:
			var fc := Cfg.color("flower")
			for k in range(3):
				var p := Vector2(32 - r * 0.6 + r * 0.6 * k, cy - r * 0.75 + (k % 2) * 3)
				_disc(img, p, 3.2, fc)
				_disc(img, p, 1.2, Color(0.95, 0.8, 0.2))
		if fruit:
			var gc := Cfg.color("fruit_green")
			for k in range(3):
				_disc(img, Vector2(32 - r * 0.7 + r * 0.7 * k, 60 - (k % 2) * 2), 3.3, gc)
		if ripe:
			var rc := Cfg.color("fruit_ripe")
			for k in range(2):
				_disc(img, Vector2(32 - r * 0.35 + r * 0.7 * k, 59), 4.0, rc)
	var tex := ImageTexture.create_from_image(img)
	_cache[key] = tex
	return tex


# 캐릭터: 몸통·머리 단순 도형. front = 화면 쪽(출입구 쪽)을 볼 때 얼굴 점이 보여요
static func character_texture(front: bool) -> ImageTexture:
	var key := "char_%s" % front
	if _cache.has(key):
		return _cache[key]
	var img := Image.create(48, 96, false, Image.FORMAT_RGBA8)
	img.fill(Color(0, 0, 0, 0))
	var body := Color("#3d6fb5")
	var legs := Color("#4a4038")
	var skin := Color("#e3b98f")
	_rect(img, Rect2i(16, 72, 6, 23), legs)
	_rect(img, Rect2i(26, 72, 6, 23), legs)
	_rect(img, Rect2i(12, 38, 24, 36), body)
	_rect(img, Rect2i(7, 40, 6, 26), body.darkened(0.15))
	_rect(img, Rect2i(35, 40, 6, 26), body.darkened(0.15))
	_disc(img, Vector2(24, 25), 12.0, skin)
	_rect(img, Rect2i(10, 10, 28, 9), Color("#c9a64a"))   # 모자 챙
	_rect(img, Rect2i(15, 4, 18, 8), Color("#c9a64a"))
	if front:
		_disc(img, Vector2(19, 27), 1.8, Color(0.1, 0.1, 0.1))
		_disc(img, Vector2(29, 27), 1.8, Color(0.1, 0.1, 0.1))
	var tex := ImageTexture.create_from_image(img)
	_cache[key] = tex
	return tex


# 바닥에 눕히는 표시: 테두리(대상 칸), 방금 한 일(도구 색 점들)
static func frame_texture(col: Color) -> ImageTexture:
	var key := "frame_%s" % col.to_html()
	if _cache.has(key):
		return _cache[key]
	var img := Image.create(64, 64, false, Image.FORMAT_RGBA8)
	img.fill(Color(0, 0, 0, 0))
	var t := 5
	_rect(img, Rect2i(0, 0, 64, t), col)
	_rect(img, Rect2i(0, 64 - t, 64, t), col)
	_rect(img, Rect2i(0, 0, t, 64), col)
	_rect(img, Rect2i(64 - t, 0, t, 64), col)
	var tex := ImageTexture.create_from_image(img)
	_cache[key] = tex
	return tex


static func marker_texture(col: Color) -> ImageTexture:
	var key := "marker_%s" % col.to_html()
	if _cache.has(key):
		return _cache[key]
	var img := Image.create(64, 64, false, Image.FORMAT_RGBA8)
	img.fill(Color(0, 0, 0, 0))
	var pts := [Vector2(14, 16), Vector2(40, 12), Vector2(26, 30), Vector2(50, 34), Vector2(12, 46), Vector2(36, 50), Vector2(54, 54)]
	for p in pts:
		_disc(img, p, 5.0, col.darkened(0.35))
		_disc(img, p, 3.6, col)
	var tex := ImageTexture.create_from_image(img)
	_cache[key] = tex
	return tex


# 칸 안의 포기 자리 8개 (칸 기준 0~1, 2줄 × 4). 자리만 조금씩 어긋나게 (고정된 값, 난수 없음)
static func plant_offsets(n: int) -> Array:
	var out := []
	var jit := [Vector2(0.03, -0.02), Vector2(-0.02, 0.03), Vector2(0.02, 0.01), Vector2(-0.03, -0.02),
			Vector2(0.01, 0.02), Vector2(-0.01, -0.03), Vector2(0.03, 0.02), Vector2(-0.02, 0.0)]
	for k in range(n):
		var col := k % 2
		var row := k / 2
		var per := maxi(1, (n + 1) / 2)
		out.append(Vector2(0.27 + 0.46 * col, (row + 0.5) / per) + jit[k % jit.size()])
	return out
