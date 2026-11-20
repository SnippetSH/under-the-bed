# C12 (m6b-character-play 9.1, 2.3·R10): 도구 줄. 평소 숨김 → 숨긴 상태에서 도구 단축키를 누르면 잠깐 올라왔다 내려감,
# 잠깐 보이는 중 다시 누르면 처음부터, Tab으로 고정·해제, 도구 고르기는 줄 상태와 무관, 고정 여부는 세이브 게임 구역에.
# 캐릭터 화면에서 실제 키(Tab, 3)로도 한 번 지나가요. 지금 든 도구 표시(알약)는 숨김일 때도 보여요.
# TODO(UI 크기, 명세 2.3·Q4): C12의 "화면 높이 3가지(1080·1440·2160)에서 UI 높이 ÷ 화면 높이가 같음"은 아직 검사하지 않아요.
#   이 파일은 도구 줄 부분만 다뤄요. UI를 화면 높이에 비례시키는 작업(다음 작업)에서 그 검사를 여기에 더해요.
#   godot --headless --path game --script res://tests/c12_toolbar.gd -- out=<폴더>
extends SceneTree

const U = preload("res://tests/util.gd")
const ToolbarState = preload("res://scripts/toolbar_state.gd")
const Driver = preload("res://tests/char_driver.gd")
const Cfg = preload("res://scripts/cfg.gd")
const SaveStore = preload("res://scripts/save_store.gd")

var fails := PackedStringArray()


func _ok(name: String, cond: bool, detail := "") -> void:
	if not cond:
		fails.push_back("%s %s" % [name, detail])
	print("%s %s %s" % ["ok  " if cond else "FAIL", name, detail])


func _run(b: Object, seconds: float, dt := 0.05) -> void:
	var n := int(round(seconds / dt))
	for i in range(n):
		b.update(dt)


func _key(cp: Node, code: Key) -> void:
	var ev := InputEventKey.new()
	ev.keycode = code
	ev.physical_keycode = code
	ev.pressed = true
	if code == KEY_TAB:
		cp._input(ev)
	else:
		cp._unhandled_input(ev)


func _initialize() -> void:
	var args := U.parse_args()
	SaveStore.dir = String(args["out"]).path_join("c12_saves")
	var slide := float(Cfg.get_v("ui", "toolbar_slide_seconds"))
	var peek := float(Cfg.get_v("ui", "toolbar_peek_seconds"))
	var def := bool(Cfg.get_v("ui", "toolbar_default_shown"))
	print("config toolbar_default_shown=%s slide=%.2f peek=%.2f" % [def, slide, peek])
	_ok("default_hidden", def == false)
	var b := ToolbarState.new()
	b.setup(def, slide, peek)
	_ok("starts_hidden_not_clickable", b.shown_t == 0.0 and not b.clickable() and not b.target_shown())
	b.peek()
	_run(b, slide * 0.5)
	_ok("peek_slides_up_half_way_at_half_slide_time", is_equal_approx(b.shown_t, 0.5), "t=%.2f" % b.shown_t)
	_run(b, slide * 0.5)
	_ok("peek_fully_shown_after_slide_time", is_equal_approx(b.shown_t, 1.0) and b.clickable())
	_run(b, peek)
	_run(b, slide)
	_ok("peek_slides_back_down", is_equal_approx(b.shown_t, 0.0) and not b.target_shown(), "t=%.2f left=%.2f" % [b.shown_t, b.peek_left])
	b.peek()
	_run(b, peek * 0.75)
	b.peek()
	_run(b, peek * 0.75)
	_ok("peek_restart_keeps_it_shown", is_equal_approx(b.shown_t, 1.0), "t=%.2f left=%.2f" % [b.shown_t, b.peek_left])
	_run(b, peek * 0.25 + slide + 0.05)
	_ok("peek_restart_then_hides", is_equal_approx(b.shown_t, 0.0))
	b.toggle()
	_run(b, slide)
	_run(b, 5.0)
	_ok("tab_pins_shown", b.pinned and is_equal_approx(b.shown_t, 1.0))
	b.peek()
	_ok("peek_ignored_while_pinned", b.peek_left == 0.0)
	b.toggle()
	_run(b, slide)
	_ok("tab_again_hides", not b.pinned and is_equal_approx(b.shown_t, 0.0))
	# 세션: 도구 고르기는 줄 상태와 무관, 저장 왕복
	var s: Object = Driver.new_session("direct_play")
	_ok("session_default_toolbar_hidden", s.toolbar_shown == false)
	s.select_tool(6)
	_ok("tool_selected_while_hidden", s.tool_index == 6)
	s.toolbar_shown = true
	var st: Dictionary = s.char_state()
	var s2: Object = Driver.new_session("direct_play")
	s2.restore_char_state(JSON.parse_string(JSON.stringify(st)))
	_ok("toolbar_pin_saved_in_game_section", s2.toolbar_shown == true and s2.tool_index == 6, str(st.get("toolbar_shown")))
	s2.restore_char_state({})
	_ok("old_save_without_key_uses_default", s2.toolbar_shown == def)
	s.dispose()
	s2.dispose()
	# 캐릭터 화면: 실제 키로
	var cp: Node = load("res://scenes/char_play.tscn").instantiate()
	get_root().add_child(cp)
	for i in range(3):
		await process_frame
	cp._process(0.016)
	_ok("screen_bar_hidden_pill_visible", not cp._bar_card.visible and cp._tool_pill.visible and cp._tool_label.text != "")
	_key(cp, KEY_3)
	for i in range(int(ceil(slide / 0.05)) + 1):
		cp._process(0.05)
	_ok("screen_tool_key_peeks_bar", cp.s.tool_index == 2 and cp._bar_card.visible and not cp.bar.pinned and cp.bar.shown_t > 0.99)
	for i in range(int(ceil((peek + slide) / 0.05)) + 2):
		cp._process(0.05)
	_ok("screen_peek_goes_back_down", not cp._bar_card.visible)
	_key(cp, KEY_TAB)
	for i in range(10):
		cp._process(0.05)
	_ok("screen_tab_pins", cp.bar.pinned and cp.s.toolbar_shown and cp._bar_card.visible)
	_key(cp, KEY_TAB)
	for i in range(10):
		cp._process(0.05)
	_ok("screen_tab_unpins", not cp.bar.pinned and not cp.s.toolbar_shown and not cp._bar_card.visible)
	cp.queue_free()
	await process_frame
	U.finish(self, fails)
