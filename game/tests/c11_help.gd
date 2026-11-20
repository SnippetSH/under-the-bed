# C11 (m6b-character-play 9.1, 2.4): 도움말. 글 파일(config/help_ko.json)을 읽고 창을 열어요.
#  1) 글: 주제 11개 + 조작법 = 12쪽, 주제마다 리드 한 줄 + 짧은 항목 3~7개(항목 목록, R19), 숫자·영문자 없음(정답 숫자 없음), 해요체
#  2) 캐릭터 화면: 일시정지 → 도움말 → 주제 바꾸기 → 닫기. 모든 주제에서 창 크기·자리가 같고(고정), 긴 글(조작법)은 스크롤,
#     열린 동안 틱·캐릭터 위치 그대로, Esc → 일시정지 메뉴(계속 멈춤) → Esc → 다시 흐름
#   godot --headless --path game --script res://tests/c11_help.gd -- out=<폴더>
extends SceneTree

const U = preload("res://tests/util.gd")
const HelpData = preload("res://scripts/help_data.gd")
const SaveStore = preload("res://scripts/save_store.gd")

var fails := PackedStringArray()


func _ok(name: String, cond: bool, detail := "") -> void:
	if not cond:
		fails.push_back("%s %s" % [name, detail])
	print("%s %s %s" % ["ok  " if cond else "FAIL", name, detail])


func _initialize() -> void:
	var args := U.parse_args()
	SaveStore.dir = String(args["out"]).path_join("c11_saves")
	_check_data()
	await _check_window()
	U.finish(self, fails)


func _check_data() -> void:
	var d: Dictionary = HelpData.data()
	var topics: Array = d.get("topics", [])
	_ok("eleven_topics_plus_controls_is_12_pages", topics.size() == 11 and HelpData.pages().size() == 12, "topics=%d" % topics.size())
	var bad_chars := RegEx.create_from_string("[A-Za-z_0-9]")
	var ids := {}
	for t in topics:
		var sents: Array = [t.get("lead", "")] + Array(t.get("points", []))
		var n := Array(t.get("points", [])).size()
		var clean := str(t.get("lead", "")) != ""
		var polite := true
		for sn in sents:
			if bad_chars.search(str(sn)) != null:
				clean = false
			var st := str(sn).strip_edges()
			if not (st.ends_with("요.") or st.ends_with("요)") or st.ends_with("요).")):
				polite = false
		_ok("topic_%s_lead_and_3_to_7_items" % t["id"], n >= 3 and n <= 7 and str(t.get("title", "")) != "", "%s: 항목 %d개" % [t["title"], n])
		_ok("topic_%s_no_digits_or_latin" % t["id"], clean and bad_chars.search(str(t["title"])) == null)
		_ok("topic_%s_haeyo_endings" % t["id"], polite)
		_ok("topic_%s_page_is_item_list" % t["id"], HelpData.pages().filter(func(p): return p["id"] == t["id"])[0]["body"].count("•") == n)
		ids[t["id"]] = true
	_ok("topic_ids_unique", ids.size() == topics.size())
	var keys: Array = d.get("keys", [])
	_ok("controls_from_key_table", keys.size() >= 10 and HelpData.controls_text().contains("Tab") and HelpData.key_hint().contains("Tab 도구 줄") and HelpData.key_hint().contains("Shift 달리기"), HelpData.key_hint())
	# 잿빛곰팡이 환기는 저녁 쪽 (Q3, house-humidity)
	var botrytis := topics.filter(func(t): return JSON.stringify(t).contains("잿빛곰팡이"))
	_ok("botrytis_ventilation_mentions_evening", not botrytis.is_empty() and JSON.stringify(botrytis).contains("저녁"))


func _check_window() -> void:
	var cp: Node = load("res://scenes/char_play.tscn").instantiate()
	get_root().add_child(cp)
	for i in range(3):
		await process_frame
	var s: Object = cp.s
	_ok("char_screen_started", s != null and cp.view != null)
	cp._open_pause()
	await process_frame
	var t0: int = s.world().get_tick()
	var p0: Vector2 = s.pos
	cp.open_help(0)
	await process_frame
	_ok("help_open_and_paused", cp._popup_kind == "help" and s.paused and cp._popup.visible)
	var rects := []
	var list_pos := []
	var close_pos := []
	var scrollable := []
	var n_pages: int = HelpData.pages().size()
	for i in range(n_pages):
		cp.open_help(i)
		await process_frame
		await process_frame
		cp._layout()
		rects.append(cp._popup.get_rect())
		var hb: Node = cp._popup_box.get_child(1)
		list_pos.append((hb.get_child(0) as Control).get_global_rect())
		close_pos.append((cp._popup_box.get_child(cp._popup_box.get_child_count() - 1) as Control).get_global_rect())
		scrollable.append(cp.help_scrollable())
	var same := true
	for i in range(1, n_pages):
		if rects[i] != rects[0] or list_pos[i] != list_pos[0] or close_pos[i] != close_pos[0]:
			same = false
	print("help window rect=%s list=%s close=%s scrollable=%s" % [rects[0], list_pos[0], close_pos[0], scrollable])
	_ok("help_window_same_rect_for_every_topic", same)
	_ok("controls_page_scrolls", scrollable[n_pages - 1])
	cp.open_help(n_pages - 1)
	await process_frame
	await process_frame
	var dn := InputEventKey.new()
	dn.keycode = KEY_PAGEDOWN
	dn.physical_keycode = KEY_PAGEDOWN
	dn.pressed = true
	cp._unhandled_input(dn)
	await process_frame
	var scrolled: int = cp.help_scroll.scroll_vertical
	cp.open_help(0)
	await process_frame
	_ok("keys_scroll_and_topic_change_resets", scrolled > 0 and cp.help_scroll.scroll_vertical == 0 and cp._popup_kind == "help", "scrolled=%d" % scrolled)
	var b0: String = cp.help_body.text
	cp.open_help(2)
	await process_frame
	_ok("help_topic_switch", cp.help_index == 2 and cp.help_body.text != b0, cp.help_body.text.substr(0, 20))
	for i in range(30):
		cp._process(1.0)
		s.character_frame(Vector2i(0, 1), false, 0.5)
	_ok("no_ticks_or_movement_while_help_open", s.world().get_tick() == t0 and s.pos == p0, "tick %d->%d pos %s->%s" % [t0, s.world().get_tick(), p0, s.pos])
	var esc := InputEventKey.new()
	esc.keycode = KEY_ESCAPE
	esc.physical_keycode = KEY_ESCAPE
	esc.pressed = true
	cp._unhandled_input(esc)
	await process_frame
	_ok("esc_returns_to_pause_menu_still_paused", cp._popup_kind == "pause" and s.paused)
	for i in range(10):
		cp._process(1.0)
	_ok("still_no_ticks_in_pause_menu", s.world().get_tick() == t0)
	cp._unhandled_input(esc)
	await process_frame
	_ok("esc_again_resumes", not s.paused and not cp._popup.visible)
	cp.queue_free()
	await process_frame
