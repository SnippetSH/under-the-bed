# 시작 화면: 판 고르기 (결정 D3) + 저장된 판 불러오기. 점검 화면 목록은 game_config.cfg [start] scenario_order,
# 캐릭터 플레이(M6b) 목록은 [start] char_scenario_order. 저장된 판은 요약의 "screen"을 보고 그 화면으로 열어요.
extends Control

const Cfg = preload("res://scripts/cfg.gd")
const Session = preload("res://scripts/session.gd")
const SaveStore = preload("res://scripts/save_store.gd")
const SavePanel = preload("res://scripts/save_panel.gd")

const CHECK_SCENE := "res://scenes/main.tscn"
const CHAR_SCENE := "res://scenes/char_play.tscn"

var _save_panel: PanelContainer
var _notice: Label


func _ready() -> void:
	var bg := ColorRect.new()
	bg.color = Cfg.color("background")
	bg.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(bg)

	var title := Label.new()
	title.text = "땅속 사정"
	title.add_theme_font_size_override("font_size", 28)
	title.position = Vector2(40, 24)
	add_child(title)
	var sub := Label.new()
	sub.text = "캐릭터 플레이(M6b)와 점검 화면(M6a, 개발·검증용)"
	sub.modulate = Color(1, 1, 1, 0.6)
	sub.position = Vector2(40, 66)
	add_child(sub)

	# 왼쪽: 새 판 (점검 화면)
	var left := VBoxContainer.new()
	left.position = Vector2(40, 108)
	left.custom_minimum_size = Vector2(560, 0)
	left.add_theme_constant_override("separation", 4)
	add_child(left)
	var ch := Label.new()
	ch.text = "점검 화면 (M6a): 격자에서 영역을 골라 명령을 넣어요"
	ch.add_theme_color_override("font_color", Cfg.color("notice"))
	left.add_child(ch)
	var ids := Cfg.scenario_ids()
	var first_watch := true
	for id in ids:
		var entry := Cfg.scenario(str(id))
		if entry.is_empty():
			push_error("시나리오 항목이 없어요: %s" % id)
			continue
		if entry["mode"] == "watch" and first_watch:
			first_watch = false
			var h := Label.new()
			h.text = "자동 처방 관전 (정해진 처방 일정표대로 돌려 봐요. 자동 물 주기는 꺼짐)"
			h.add_theme_color_override("font_color", Cfg.color("notice"))
			left.add_child(Control.new())
			left.add_child(h)
		left.add_child(_scenario_button(entry))

	# 오른쪽: 캐릭터 플레이 + 이어하기 + 저장 목록
	var right := VBoxContainer.new()
	right.position = Vector2(640, 108)
	right.custom_minimum_size = Vector2(600, 0)
	right.add_theme_constant_override("separation", 8)
	add_child(right)
	var cph := Label.new()
	cph.text = "캐릭터 플레이 (M6b): 캐릭터가 걸어가서 칸마다 일해요"
	cph.add_theme_color_override("font_color", Cfg.color("notice"))
	right.add_child(cph)
	for id in Array(Cfg.get_v("start", "char_scenario_order")):
		var entry := Cfg.scenario(str(id))
		if entry.is_empty():
			push_error("캐릭터 플레이 시나리오 항목이 없어요: %s" % id)
			continue
		right.add_child(_scenario_button(entry, CHAR_SCENE, "캐릭터 플레이: "))
	var cont := Button.new()
	cont.custom_minimum_size = Vector2(0, 44)
	var latest := SaveStore.latest_loadable()
	cont.text = "이어하기 (가장 최근 저장)" if latest != "" else "이어하기 (불러올 수 있는 저장이 없어요)"
	cont.disabled = latest == ""
	cont.pressed.connect(func(): _load(latest))
	right.add_child(cont)
	_save_panel = SavePanel.new()
	_save_panel.custom_minimum_size = Vector2(600, 330)
	right.add_child(_save_panel)
	_save_panel.setup(false, false)
	_save_panel.load_requested.connect(_load)

	_notice = Label.new()
	_notice.position = Vector2(40, 660)
	_notice.size = Vector2(1200, 40)
	_notice.add_theme_color_override("font_color", Cfg.color("warning"))
	_notice.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	add_child(_notice)

	var hint := Label.new()
	hint.text = "점검 화면: Space 정지 · 1~4 배속 · F3 개발자 패널 · F5 빠른 저장 · F9 빠른 불러오기    캐릭터 플레이: Esc 메뉴의 도움말에 조작법"
	hint.modulate = Color(1, 1, 1, 0.6)
	hint.position = Vector2(40, 692)
	add_child(hint)


func _scenario_button(entry: Dictionary, scene: String = CHECK_SCENE, prefix: String = "") -> Control:
	var box := VBoxContainer.new()
	var b := Button.new()
	b.text = prefix + str(entry["label"])
	b.custom_minimum_size = Vector2(0, 38)
	b.alignment = HORIZONTAL_ALIGNMENT_LEFT
	var id: String = entry["id"]
	b.pressed.connect(func():
		Session.choose_scenario(id)
		get_tree().change_scene_to_file(scene))
	box.add_child(b)
	var d := Label.new()
	d.text = entry["description"]
	d.add_theme_font_size_override("font_size", 12)
	d.modulate = Color(1, 1, 1, 0.65)
	d.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART   # 긴 설명이 열 폭을 넓히지 않게
	d.custom_minimum_size = Vector2(540, 0)
	box.add_child(d)
	return box


# 저장 칸을 불러와요. 불러올 수 있는지는 본 화면이 다시 확인하므로, 여기서는 목록에서 눌려 있던 칸만 넘겨요.
# 캐릭터 화면에서 저장한 판(요약 screen = "character")은 캐릭터 화면으로, 나머지(M6a 세이브 포함)는 점검 화면으로 열어요.
func _load(id: String) -> void:
	Session.choose_save(id)
	get_tree().change_scene_to_file(scene_for_save(id))


static func scene_for_save(id: String) -> String:
	for e in SaveStore.list_saves():
		if e["id"] == id:
			return CHAR_SCENE if SaveStore.screen_of(e) == SaveStore.SCREEN_CHARACTER else CHECK_SCENE
	return CHECK_SCENE
