# 부드러운 임시 2D 그림 (M6b P7): 캐릭터 평면 그림, 도구 그림, 대상 칸 빛, "방금 한 일" 표시, 그림자, 바닥 무늬, 해·달.
# 모두 이 파일에서 직접 짠 SVG를 Image.load_svg_from_string으로 그려요(안티에일리어싱, 곡선, 그라데이션). 내려받은 그림·글꼴은 없어요.
# 포기·구조물·장치는 3D 모델이에요(art_library.gd: 디렉터 모델 또는 placeholder_meshes.gd 자리 채움).
extends RefCounted

static var _cache := {}

# 따뜻하고 부드러운 색 (게임용 임의값, 그림 전용)
const C_OUTLINE := "#4a3b2c"


static func _svg(w: float, h: float, vb_w: float, vb_h: float, body: String, defs := "") -> String:
	return '<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d"><defs>%s</defs>%s</svg>' % [int(w), int(h), int(vb_w), int(vb_h), defs, body]


static func _raster(key: String, svg: String, scale := 1.0) -> ImageTexture:
	if _cache.has(key):
		return _cache[key]
	var img := Image.new()
	var e := img.load_svg_from_string(svg, scale)
	if e != OK:
		push_error("SVG 그리기 실패 %s (%d)" % [key, e])
		img = Image.create(4, 4, false, Image.FORMAT_RGBA8)
	img.fix_alpha_edges()   # 투명 가장자리의 색을 번지게 해서 선형 필터링 때 검은 테두리가 안 생기게
	img.generate_mipmaps()
	var tex := ImageTexture.create_from_image(img)
	_cache[key] = tex
	return tex


static func _lin(id: String, c0: String, c1: String, x1 := 0.0, y1 := 0.0, x2 := 0.0, y2 := 1.0) -> String:
	return '<linearGradient id="%s" x1="%.2f" y1="%.2f" x2="%.2f" y2="%.2f"><stop offset="0" stop-color="%s"/><stop offset="1" stop-color="%s"/></linearGradient>' % [id, x1, y1, x2, y2, c0, c1]


static func _rad(id: String, c0: String, a0: float, c1: String, a1: float) -> String:
	return '<radialGradient id="%s" cx="0.5" cy="0.5" r="0.5"><stop offset="0" stop-color="%s" stop-opacity="%.2f"/><stop offset="1" stop-color="%s" stop-opacity="%.2f"/></radialGradient>' % [id, c0, a0, c1, a1]


# ---------- 바닥 그림자 (포기 묶음·캐릭터 밑) ----------
static func shadow_texture() -> ImageTexture:
	var defs := _rad("sh", "#2a1d10", 0.42, "#2a1d10", 0.0)
	return _raster("shadow", _svg(128, 128, 100, 100, '<ellipse cx="50" cy="50" rx="50" ry="50" fill="url(#sh)"/>', defs))


# ---------- 캐릭터: 둥근 몸, 밀짚모자. dir = "front" / "back" / "side", frame 0·1 (다리) ----------
const CHAR_VB := Vector2(64, 100)


static func character_texture(dir: String, frame: int) -> ImageTexture:
	var ck := "char_%s_%d" % [dir, frame]
	if _cache.has(ck):
		return _cache[ck]
	var o := 'stroke="%s" stroke-opacity="0.55" stroke-width="1.4"' % C_OUTLINE
	var defs := _lin("shirt", "#fbe7c4", "#e9c995") + _lin("ov", "#6fa58c", "#4c7f6a") + _lin("hat", "#f7dc8f", "#d7b25c")
	defs += _lin("skin", "#f7d3b0", "#eab48b")
	var b := ""
	# 다리 (걷기 두 그림: 한쪽 다리를 들어요)
	var lift := 4.0 if frame == 1 else 0.0
	if dir == "side":
		b += '<rect x="25" y="%.1f" width="9" height="%.1f" rx="4" fill="#5d7f9a" %s/>' % [72 - lift * 0.5, 22 - lift * 0.5, o]
		b += '<rect x="%.1f" y="72" width="9" height="22" rx="4" fill="#4f6f88" %s/>' % [30 + (5 if frame == 1 else 0), o]
		b += '<ellipse cx="%.1f" cy="95" rx="7" ry="3.5" fill="#6b4a32"/>' % [36 + (5 if frame == 1 else 0)]
	else:
		b += '<rect x="21" y="72" width="9" height="%.1f" rx="4" fill="#5d7f9a" %s/>' % [22 - lift, o]
		b += '<rect x="34" y="72" width="9" height="%.1f" rx="4" fill="#5d7f9a" %s/>' % [22 - (4.0 - lift), o]
		b += '<ellipse cx="25.5" cy="%.1f" rx="6" ry="3.4" fill="#6b4a32"/>' % [94 - lift]
		b += '<ellipse cx="38.5" cy="%.1f" rx="6" ry="3.4" fill="#6b4a32"/>' % [94 - (4.0 - lift)]
	# 몸통 (셔츠 + 멜빵바지)
	if dir == "side":
		b += '<rect x="21" y="44" width="22" height="32" rx="10" fill="url(#shirt)" %s/>' % o
		b += '<rect x="22" y="56" width="20" height="21" rx="7" fill="url(#ov)" %s/>' % o
		b += '<rect x="%.1f" y="47" width="8" height="22" rx="4" fill="url(#shirt)" %s transform="rotate(%d 32 49)"/>' % [28.0, o, -14 if frame == 1 else 10]
		b += '<circle cx="%.1f" cy="69" r="3.6" fill="url(#skin)" %s/>' % [33 + (5 if frame == 0 else -4), o]
	else:
		b += '<rect x="15" y="44" width="34" height="32" rx="13" fill="url(#shirt)" %s/>' % o
		b += '<rect x="18" y="55" width="28" height="22" rx="9" fill="url(#ov)" %s/>' % o
		b += '<path d="M21,56 L23,45 M43,56 L41,45" stroke="#4c7f6a" stroke-width="3" stroke-linecap="round"/>'
		if dir == "front":
			b += '<circle cx="23" cy="61" r="1.4" fill="#f2e3a0"/><circle cx="41" cy="61" r="1.4" fill="#f2e3a0"/>'
			b += '<rect x="27" y="62" width="10" height="7" rx="2" fill="#5d9580"/>'
		var sw := 3.0 if frame == 1 else -3.0
		b += '<rect x="8" y="%.1f" width="8" height="22" rx="4" fill="url(#shirt)" %s/>' % [47 + sw * 0.4, o]
		b += '<rect x="48" y="%.1f" width="8" height="22" rx="4" fill="url(#shirt)" %s/>' % [47 - sw * 0.4, o]
		b += '<circle cx="12" cy="%.1f" r="3.8" fill="url(#skin)" %s/>' % [70 + sw * 0.4, o]
		b += '<circle cx="52" cy="%.1f" r="3.8" fill="url(#skin)" %s/>' % [70 - sw * 0.4, o]
	# 머리
	if dir == "back":
		b += '<circle cx="32" cy="30" r="15" fill="#7a5236" %s/>' % o
		b += '<path d="M19,34 Q32,46 45,34" fill="#6a452c"/>'
	elif dir == "side":
		b += '<circle cx="32" cy="30" r="15" fill="url(#skin)" %s/>' % o
		b += '<path d="M17,28 Q20,16 33,16 Q27,24 26,36 Q20,36 17,28 Z" fill="#7a5236"/>'
		b += '<ellipse cx="40" cy="29" rx="1.6" ry="2.2" fill="#3a2a20"/>'
		b += '<circle cx="46.5" cy="32" r="2.2" fill="#eab48b"/>'
		b += '<circle cx="37" cy="35" r="2.6" fill="#f29a8a" fill-opacity="0.55"/>'
	else:
		b += '<circle cx="32" cy="30" r="15" fill="url(#skin)" %s/>' % o
		b += '<path d="M18,27 Q22,17 32,17 Q42,17 46,27 Q40,22 32,23 Q24,22 18,27 Z" fill="#7a5236"/>'
		b += '<ellipse cx="26.5" cy="30" rx="1.7" ry="2.3" fill="#3a2a20"/><ellipse cx="37.5" cy="30" rx="1.7" ry="2.3" fill="#3a2a20"/>'
		b += '<circle cx="26.9" cy="29.2" r="0.6" fill="#ffffff"/><circle cx="37.9" cy="29.2" r="0.6" fill="#ffffff"/>'
		b += '<circle cx="23" cy="35" r="2.8" fill="#f29a8a" fill-opacity="0.5"/><circle cx="41" cy="35" r="2.8" fill="#f29a8a" fill-opacity="0.5"/>'
		b += '<path d="M28.5,37 Q32,40 35.5,37" stroke="#8a4a3a" stroke-width="1.3" fill="none" stroke-linecap="round"/>'
	# 밀짚모자: 넓은 챙 + 둥근 꼭대기 + 띠
	b += '<ellipse cx="32" cy="18" rx="24" ry="6.5" fill="url(#hat)" %s/>' % o
	b += '<path d="M19,17 Q19,4 32,4 Q45,4 45,17 Z" fill="url(#hat)" %s/>' % o
	b += '<path d="M19.5,14.5 Q32,18.5 44.5,14.5 L44.8,17 Q32,21 19.2,17 Z" fill="#c8564a"/>'
	var tex := _raster(ck, _svg(CHAR_VB.x * 2, CHAR_VB.y * 2, CHAR_VB.x, CHAR_VB.y, b, defs))
	return tex


# ---------- 바닥 무늬 ----------
static func noise_texture(key: String, c0: Color, c1: Color, freq: float, size := 256) -> ImageTexture:
	if _cache.has(key):
		return _cache[key]
	var n := FastNoiseLite.new()
	n.seed = 7   # 고정값 (꾸밈)
	n.frequency = freq
	n.fractal_octaves = 3
	var src := n.get_seamless_image(size, size)
	var img := Image.create(size, size, false, Image.FORMAT_RGBA8)
	for y in range(size):
		for x in range(size):
			var v := src.get_pixel(x, y).r
			img.set_pixel(x, y, c0.lerp(c1, smoothstep(0.2, 0.8, v)))
	img.generate_mipmaps()
	var tex := ImageTexture.create_from_image(img)
	_cache[key] = tex
	return tex


# ---------- 대상 칸 테두리 (부드러운 빛) ----------
static func target_texture() -> ImageTexture:
	var b := ""
	var layers := [[14.0, 0.10], [10.0, 0.18], [7.0, 0.32], [3.6, 0.95]]
	for l in layers:
		b += '<rect x="9" y="9" width="82" height="82" rx="16" fill="none" stroke="#fff3b8" stroke-opacity="%.2f" stroke-width="%.1f"/>' % [l[1], l[0]]
	b += '<rect x="9" y="9" width="82" height="82" rx="16" fill="#fff6c8" fill-opacity="0.10"/>'
	return _raster("target", _svg(128, 128, 100, 100, b))


# ---------- "방금 한 일" 표시: 물방울 / 가루 / 흙 고랑 / 필름 반짝 / 잎 조각 / 흙먼지 / 작은 벌레 ----------
static func marker_texture(kind: String, col: Color) -> ImageTexture:
	var ck := "mk_%s_%s" % [kind, col.to_html(false)]
	if _cache.has(ck):
		return _cache[ck]
	var c := "#" + col.to_html(false)
	var dark := "#" + col.darkened(0.35).to_html(false)
	var b := ""
	var pts := [Vector2(22, 26), Vector2(52, 18), Vector2(76, 34), Vector2(34, 52), Vector2(64, 58), Vector2(20, 78), Vector2(48, 82), Vector2(80, 74)]
	match kind:
		"water":
			for p in pts:
				b += '<ellipse cx="%.1f" cy="%.1f" rx="7" ry="2.6" fill="#bfe3ff" fill-opacity="0.55"/>' % [p.x, p.y + 6]
				b += '<path d="M%.1f,%.1f C%.1f,%.1f %.1f,%.1f %.1f,%.1f C%.1f,%.1f %.1f,%.1f %.1f,%.1f Z" fill="#6fb8f2" stroke="#3f86c8" stroke-width="1"/>' % [
					p.x, p.y - 9, p.x + 5, p.y - 2, p.x + 5, p.y + 3, p.x, p.y + 3, p.x - 5, p.y + 3, p.x - 5, p.y - 2, p.x, p.y - 9]
				b += '<circle cx="%.1f" cy="%.1f" r="1.3" fill="#ffffff" fill-opacity="0.8"/>' % [p.x - 1.6, p.y - 1.5]
		"powder":
			for i in range(40):
				var p := Vector2(fmod(i * 37.0, 84.0) + 8.0, fmod(i * 53.0 + 11.0, 84.0) + 8.0)
				var r := 1.6 + fmod(i * 0.7, 1.8)
				b += '<circle cx="%.1f" cy="%.1f" r="%.1f" fill="%s" stroke="%s" stroke-width="0.6"/>' % [p.x, p.y, r, c, dark]
		"furrow":
			for y in [28, 50, 72]:
				b += '<path d="M14,%d Q50,%d 86,%d" stroke="#7a5230" stroke-opacity="0.75" stroke-width="5" fill="none" stroke-linecap="round"/>' % [y, y - 8, y]
				b += '<path d="M14,%d Q50,%d 86,%d" stroke="#c99a68" stroke-opacity="0.6" stroke-width="2" fill="none" stroke-linecap="round"/>' % [y - 3, y - 11, y - 3]
		"sheen":
			for p in [Vector2(30, 30), Vector2(70, 50), Vector2(40, 74)]:
				b += '<path d="M%.1f,%.1f L%.1f,%.1f L%.1f,%.1f L%.1f,%.1f Z" fill="#ffffff" fill-opacity="0.9"/>' % [p.x, p.y - 9, p.x + 3, p.y, p.x, p.y + 9, p.x - 3, p.y]
				b += '<path d="M%.1f,%.1f L%.1f,%.1f" stroke="#ffffff" stroke-opacity="0.9" stroke-width="2" stroke-linecap="round"/>' % [p.x - 8, p.y, p.x + 8, p.y]
		"petals":
			for p in pts:
				b += '<ellipse cx="%.1f" cy="%.1f" rx="4" ry="2.4" fill="%s" stroke="%s" stroke-width="0.6" transform="rotate(%d %.1f %.1f)"/>' % [p.x, p.y, c, dark, int(p.x * 7) % 180, p.x, p.y]
		"dust":
			for p in [Vector2(36, 40), Vector2(60, 56), Vector2(44, 66)]:
				b += '<circle cx="%.1f" cy="%.1f" r="12" fill="#cdb08a" fill-opacity="0.45"/>' % [p.x, p.y]
				b += '<circle cx="%.1f" cy="%.1f" r="7" fill="#e3cfae" fill-opacity="0.5"/>' % [p.x + 4, p.y - 3]
		"bugs":
			for p in pts:
				b += '<path d="M%.1f,%.1f l-4,-3 M%.1f,%.1f l4,-3 M%.1f,%.1f l-4,3 M%.1f,%.1f l4,3" stroke="#5a3a20" stroke-width="0.9"/>' % [p.x, p.y, p.x, p.y, p.x, p.y, p.x, p.y]
				b += '<ellipse cx="%.1f" cy="%.1f" rx="3.2" ry="2.4" fill="%s" stroke="%s" stroke-width="0.6"/>' % [p.x, p.y, c, dark]
	return _raster(ck, _svg(128, 128, 100, 100, b))


# ---------- 도구 줄 그림 (64 px) ----------
# px: 화면에 그려질 크기 (그 크기로 다시 그려서 선명하게)
static func tool_icon(tool_id: String, px := 128) -> ImageTexture:
	var ck := "icon_%s_%d" % [tool_id, px]
	if _cache.has(ck):
		return _cache[ck]
	var o := 'stroke="%s" stroke-opacity="0.7" stroke-width="2" stroke-linejoin="round"' % C_OUTLINE
	var defs := _lin("ic_teal", "#86d0c4", "#4e9c90") + _lin("ic_sack", "#fbf6ea", "#ddd2b8") + _lin("ic_wood", "#d8a86a", "#a8743f")
	defs += _lin("ic_metal", "#eef1f3", "#aab4bc") + _lin("ic_brown", "#a77a52", "#6b4a32") + _lin("ic_dark", "#55595f", "#2c2f33")
	var b := ""
	match tool_id:
		"watering_can":
			b += '<path d="M24,40 L28,78 Q30,84 38,84 L62,84 Q70,84 72,78 L76,40 Z" fill="url(#ic_teal)" %s/>' % o
			b += '<path d="M74,52 L92,34" stroke="#4e9c90" stroke-width="7" stroke-linecap="round"/><ellipse cx="92" cy="33" rx="5" ry="3" fill="#86d0c4" %s/>' % o
			b += '<path d="M34,40 Q50,14 66,40" fill="none" stroke="#4e9c90" stroke-width="6" stroke-linecap="round"/>'
			b += '<path d="M90,46 C88,50 88,52 90,53 C92,52 92,50 90,46 Z M84,52 C82,56 82,58 84,59 C86,58 86,56 84,52 Z" fill="#6fb8f2"/>'
		"fertilizer", "lime":
			var spot := "#9fc4e6" if tool_id == "fertilizer" else "#c9c4b6"
			b += '<path d="M26,30 Q24,20 34,22 L66,22 Q76,20 74,30 L78,82 Q78,90 70,90 L30,90 Q22,90 22,82 Z" fill="url(#ic_sack)" %s/>' % o
			b += '<path d="M30,26 Q50,34 70,26" fill="none" stroke="#b9ad92" stroke-width="2"/>'
			b += '<circle cx="50" cy="58" r="14" fill="%s" fill-opacity="0.8"/>' % spot
			b += '<circle cx="45" cy="55" r="3" fill="#ffffff"/><circle cx="54" cy="61" r="3" fill="#ffffff"/><circle cx="52" cy="52" r="2.4" fill="#ffffff"/>'
		"compost":
			b += '<path d="M12,84 Q22,46 50,42 Q78,46 88,84 Z" fill="url(#ic_brown)" %s/>' % o
			b += '<circle cx="36" cy="66" r="3" fill="#4a3220"/><circle cx="58" cy="60" r="3" fill="#4a3220"/><circle cx="66" cy="74" r="3" fill="#4a3220"/>'
			b += '<path d="M50,44 Q48,30 50,22" stroke="#4f9a44" stroke-width="3" fill="none" stroke-linecap="round"/><ellipse cx="43" cy="26" rx="7" ry="4" fill="#7cc066"/><ellipse cx="57" cy="22" rx="7" ry="4" fill="#6bb35a"/>'
		"hoe":
			b += '<path d="M22,88 L72,20" stroke="url(#ic_wood)" stroke-width="8" stroke-linecap="round"/>'
			b += '<path d="M62,14 Q86,14 90,34 L76,36 Q74,26 64,26 Z" fill="url(#ic_metal)" %s/>' % o
		"mulch":
			b += '<path d="M30,40 L86,40 L86,82 L44,82 Z" fill="#3a3d42" fill-opacity="0.85"/>'
			b += '<rect x="14" y="26" width="34" height="34" rx="17" fill="url(#ic_dark)" %s/>' % o
			b += '<circle cx="31" cy="43" r="7" fill="#9aa0a8"/>'
			b += '<path d="M54,52 L78,52" stroke="#ffffff" stroke-opacity="0.35" stroke-width="3" stroke-linecap="round"/>'
		"disinfectant":
			b += '<rect x="30" y="40" width="40" height="50" rx="12" fill="#cfe27a" %s/>' % o
			b += '<rect x="40" y="26" width="20" height="16" rx="4" fill="#8aa0ad" %s/>' % o
			b += '<path d="M58,30 L76,24 L76,32 Z" fill="#6c7d88"/>'
			b += '<circle cx="84" cy="22" r="2.4" fill="#cfe27a"/><circle cx="88" cy="30" r="2" fill="#cfe27a"/><circle cx="82" cy="34" r="1.6" fill="#cfe27a"/>'
			b += '<rect x="38" y="56" width="24" height="16" rx="4" fill="#ffffff" fill-opacity="0.7"/>'
		"inoculant":
			b += '<rect x="28" y="34" width="44" height="54" rx="12" fill="#e8f6ff" fill-opacity="0.9" %s/>' % o
			b += '<rect x="30" y="22" width="40" height="14" rx="5" fill="#7cc066" %s/>' % o
			for p in [Vector2(40, 54), Vector2(56, 50), Vector2(48, 66), Vector2(60, 74), Vector2(38, 76)]:
				b += '<circle cx="%.0f" cy="%.0f" r="5" fill="#8fd48a" stroke="#4f9a44" stroke-width="1.2"/>' % [p.x, p.y]
		"scissors":
			b += '<circle cx="30" cy="70" r="11" fill="none" stroke="#e08a5d" stroke-width="6"/><circle cx="70" cy="70" r="11" fill="none" stroke="#e08a5d" stroke-width="6"/>'
			b += '<path d="M36,60 L66,16 L60,14 Z" fill="url(#ic_metal)" %s/><path d="M64,60 L34,16 L40,14 Z" fill="url(#ic_metal)" %s/>' % [o, o]
			b += '<circle cx="50" cy="38" r="3" fill="#7a8a96"/>'
		"pull":
			b += '<path d="M50,58 L50,86 M50,70 L40,84 M50,74 L60,86" stroke="#c9a06a" stroke-width="3" stroke-linecap="round"/>'
			b += '<ellipse cx="38" cy="46" rx="12" ry="7" fill="#7cc066" %s transform="rotate(-25 38 46)"/>' % o
			b += '<ellipse cx="62" cy="46" rx="12" ry="7" fill="#6bb35a" %s transform="rotate(25 62 46)"/>' % o
			b += '<path d="M50,30 L50,8 M42,16 L50,8 L58,16" stroke="#e08a5d" stroke-width="5" fill="none" stroke-linecap="round" stroke-linejoin="round"/>'
		"predators":
			b += '<rect x="26" y="30" width="48" height="58" rx="14" fill="#e8f6ff" fill-opacity="0.9" %s/>' % o
			b += '<rect x="30" y="18" width="40" height="14" rx="5" fill="#e0a24a" %s/>' % o
			for p in [Vector2(42, 52), Vector2(58, 60), Vector2(46, 74)]:
				b += '<path d="M%.0f,%.0f l-6,-4 M%.0f,%.0f l6,-4 M%.0f,%.0f l-6,4 M%.0f,%.0f l6,4" stroke="#5a3a20" stroke-width="1.5"/>' % [p.x, p.y, p.x, p.y, p.x, p.y, p.x, p.y]
				b += '<ellipse cx="%.0f" cy="%.0f" rx="5" ry="4" fill="#f08a3c" stroke="#a8541e" stroke-width="1"/>' % [p.x, p.y]
	return _raster(ck, _svg(px, px, 100, 100, b, defs))


# 해·달 (HUD 시계 옆)
static func sky_icon(day: bool, px := 64) -> ImageTexture:
	if day:
		var b := ""
		for i in range(8):
			b += '<path d="M50,8 L50,20" stroke="#f6b73c" stroke-width="6" stroke-linecap="round" transform="rotate(%d 50 50)"/>' % (i * 45)
		b += '<circle cx="50" cy="50" r="22" fill="#ffd36a" stroke="#f0a23a" stroke-width="3"/>'
		return _raster("sun_%d" % px, _svg(px, px, 100, 100, b))
	return _raster("moon_%d" % px, _svg(px, px, 100, 100, '<path d="M64,14 A36,36 0 1 0 86,66 A28,28 0 1 1 64,14 Z" fill="#f2ead0" stroke="#c9bd94" stroke-width="3"/>'))


# 화면 가장자리를 살짝 어둡게 (꾸밈)
static func vignette_texture() -> GradientTexture2D:
	if _cache.has("vignette"):
		return _cache["vignette"]
	var g := Gradient.new()
	g.set_color(0, Color(0.15, 0.1, 0.05, 0.0))
	g.set_color(1, Color(0.15, 0.1, 0.05, 0.38))
	g.add_point(0.62, Color(0.15, 0.1, 0.05, 0.0))
	var t := GradientTexture2D.new()
	t.gradient = g
	t.fill = GradientTexture2D.FILL_RADIAL
	t.fill_from = Vector2(0.5, 0.5)
	t.fill_to = Vector2(1.05, 1.05)
	t.width = 256
	t.height = 256
	_cache["vignette"] = t
	return t
