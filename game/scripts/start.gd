# 시작 화면: 판 고르기 (결정 D3). 직접 플레이 / 빈 밭 직접 플레이 / 자동 처방 관전 / 이어하기
extends Control

const Session = preload("res://scripts/session.gd")
const SaveStore = preload("res://scripts/save_store.gd")


func _ready() -> void:
	var bg := ColorRect.new()
	bg.color = Color(0.13, 0.15, 0.13)
	bg.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(bg)
	var vb := VBoxContainer.new()
	vb.position = Vector2(420, 150)
	vb.custom_minimum_size = Vector2(440, 0)
	vb.add_theme_constant_override("separation", 12)
	add_child(vb)
	var t := Label.new()
	t.text = "침대 밑 농장 (프로토타입 M6)"
	t.add_theme_font_size_override("font_size", 28)
	vb.add_child(t)
	_add(vb, "직접 플레이 (season_standard 밭·기상, 일정표 끔)", Session.MODE_DIRECT)
	_add(vb, "직접 플레이: 빈 밭 (empty 시나리오, 정식부터)", Session.MODE_EMPTY)
	_add(vb, "자동 처방 관전 (일정표 켬, 자동 물 주기 꺼짐)", Session.MODE_WATCH)
	var l := _add(vb, "이어하기 (빠른 저장 불러오기)", Session.MODE_LOAD)
	l.disabled = not SaveStore.exists()
	var h := Label.new()
	h.text = "Space 정지 · 1~4 배속 · F3 개발자 패널 · F5 저장 · F9 불러오기"
	h.modulate = Color(1, 1, 1, 0.6)
	vb.add_child(h)


func _add(parent: Node, text: String, mode: String) -> Button:
	var b := Button.new()
	b.text = text
	b.custom_minimum_size = Vector2(0, 44)
	b.pressed.connect(func():
		Session.mode = mode
		get_tree().change_scene_to_file("res://scenes/main.tscn"))
	parent.add_child(b)
	return b
