# C6 (m6b-character-play 9.1, 8장): 캐릭터 판 도중 저장 → 새 세션에서 불러오기 → 해시, 캐릭터 위치·방향, 도구·도구 설정,
# 속도, 자동 물 주기 설정이 같고, 이어 돌린 최종 해시도 저장하지 않은 판과 같음. M6a 세이브 묶음(SaveStore) 그대로예요.
# 덤:
#  - 대기 중인 행동과 구간 묶음(stroke)이 저장 왕복 뒤에도 같고, Shift+Z가 저장한 묶음을 그대로 취소함
#  - 자러 가기·며칠 맡기기 도중에는 저장하지 않음(F5 거절, 자동 저장은 미뤘다가 깬 뒤에 한 번)
#  - "character"가 없는 M6a 점검 화면 세이브를 캐릭터 화면이 읽고(캐릭터는 출입문 앞), 캐릭터 화면 세이브를 점검 화면(SimRunner)이 읽음
#  - 요약의 screen(시작 화면이 어느 화면으로 열지)
#   godot --headless --path game --script res://tests/c06_save_roundtrip.gd -- out=<폴더>
extends SceneTree

const U = preload("res://tests/util.gd")
const Plan = preload("res://tests/char_plan.gd")
const Driver = preload("res://tests/char_driver.gd")
const CharSession = preload("res://scripts/char_session.gd")
const SimRunner = preload("res://scripts/sim_runner.gd")
const SaveStore = preload("res://scripts/save_store.gd")
const Cfg = preload("res://scripts/cfg.gd")

const SLOT := "slot_1"
const HELD_SLOT := "slot_2"
const CHECK_SLOT := "slot_3"

var fails := PackedStringArray()
var saved := {}


func _ok(name: String, cond: bool, detail := "") -> void:
	if not cond:
		fails.push_back("%s %s" % [name, detail])
	print("%s %s %s" % ["ok  " if cond else "FAIL", name, detail])


func _on_save(s: Object, i: int) -> void:
	var sv: Dictionary = s.save_slot(SLOT)
	saved.merge({"ok": sv["ok"], "op": i, "hash": s.world().state_hash(), "tick": s.world().get_tick(),
			"pos": s.pos, "facing": s.facing, "tool_index": s.tool_index, "settings": s.tool_settings.duplicate(true),
			"auto": s.runner.auto_irrigation.duplicate(), "speed": s.speed_index, "toolbar": s.toolbar_shown})


func _slot_entry(id: String) -> Dictionary:
	for e in SaveStore.list_saves():
		if e["id"] == id:
			return e
	return {}


func _init() -> void:
	var args := U.parse_args()
	SaveStore.dir = String(args["out"]).path_join("c06_saves")
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(SaveStore.dir))
	for f in DirAccess.get_files_at(ProjectSettings.globalize_path(SaveStore.dir)):
		DirAccess.remove_absolute(ProjectSettings.globalize_path(SaveStore.dir.path_join(f)))

	# ---- 기준 판 (저장 지점에서 slot_1에 저장) ----
	var a: Object = Driver.new_session(Plan.SCENARIO_ID)
	a.toolbar_shown = true
	var ra: Dictionary = Plan.run(a, 0, _on_save)
	var final_a: String = a.world().state_hash()
	_ok("base_run_ok", ra["ok"], str(ra))
	_ok("C6_saved", saved.get("ok", false), "op=%d tick=%d" % [saved.get("op", -1), saved.get("tick", -1)])
	var e1 := _slot_entry(SLOT)
	_ok("summary_screen_is_character", e1.get("can_load", false) and SaveStore.screen_of(e1) == SaveStore.SCREEN_CHARACTER, str(e1.get("summary", {}).get("screen", "")))

	# ---- 새 세션에서 불러오기 ----
	var b: Object = CharSession.new()
	var lr: Dictionary = b.load_slot(SLOT)
	_ok("C6_loaded", lr["ok"], str(lr))
	if lr["ok"]:
		_ok("C6_hash_same", b.world().state_hash() == saved["hash"] and b.world().get_tick() == saved["tick"])
		_ok("C6_position_facing_same", b.pos == saved["pos"] and b.facing == saved["facing"], "%s %s" % [b.pos, b.facing])
		_ok("C6_tool_and_settings_same", b.tool_index == saved["tool_index"] and b.tool_settings == saved["settings"], str(b.tool_settings))
		_ok("C6_auto_irrigation_same", b.runner.auto_irrigation == saved["auto"])
		_ok("C6_speed_and_toolbar_same", b.speed_index == saved["speed"] and b.toolbar_shown == saved["toolbar"], "speed_index=%d toolbar=%s" % [b.speed_index, b.toolbar_shown])
		var rb: Dictionary = Plan.run(b, int(saved["op"]) + 1)
		_ok("C6_resume_final_hash_same", rb["ok"] and b.world().state_hash() == final_a, b.world().state_hash())
	b.dispose()
	a.dispose()

	# ---- 대기 중인 행동과 구간 묶음 ----
	var h: Object = Driver.new_session(Plan.SCENARIO_ID)
	var dh: Object = Driver.new(h)
	dh.use_tool(3, 0)                                        # 석회 1칸 (묶음 1)
	dh.hold_walk(1, 1, 2, -0.6, 6.6)                         # 퇴비 2행 0~6열 (묶음 2, 7칸)
	dh.open_device("heater")
	h.device_command({"action": "set_heater", "on": true, "setpoint_degC": 9.0})   # 장치 (묶음 3)
	var held_before: Array = h.held.duplicate(true)
	var markers_before: Dictionary = h.markers.duplicate(true)
	_ok("held_saved_slot", h.save_slot(HELD_SLOT)["ok"])
	var hl: Object = CharSession.new()
	_ok("held_loaded", hl.load_slot(HELD_SLOT)["ok"])
	var same: bool = hl.held.size() == held_before.size() and hl.markers == markers_before
	if same:
		for i in range(hl.held.size()):
			var x: Dictionary = hl.held[i]
			var y: Dictionary = held_before[i]
			if JSON.stringify(x["cmd"]) != JSON.stringify(y["cmd"]) or x["cell"] != y["cell"] or x["stroke"] != y["stroke"] or x["tool"] != y["tool"]:
				same = false
	_ok("held_actions_and_strokes_roundtrip", same, "held=%d" % hl.held.size())
	var types_ok := true
	for e in hl.held:
		for v in e["cmd"].get("area", []):
			if typeof(v) != TYPE_INT:
				types_ok = false
	_ok("held_area_ints_after_json", types_ok)
	hl.cancel_last_stroke()   # 장치 묶음
	var st: Array = hl.cancel_last_stroke()   # 퇴비 7칸 묶음
	_ok("shift_Z_after_load_removes_saved_stroke", st.size() == 7 and hl.held.size() == 1, "removed=%d left=%d" % [st.size(), hl.held.size()])
	# 불러온 대기 행동은 다음 시간에 평소대로 들어가요: 같은 판을 저장 없이 이어 간 것과 해시가 같음
	var hl2: Object = CharSession.new()
	hl2.load_slot(HELD_SLOT)
	hl2.step_ticks(3)
	h.step_ticks(3)
	_ok("held_loaded_then_flushed_same_hash", hl2.world().state_hash() == h.world().state_hash() and hl2.world().get_command_log().size() == h.world().get_command_log().size())
	hl.dispose()
	hl2.dispose()

	# ---- 자는 동안 저장 안 함, 자동 저장은 깬 뒤에 ----
	var z: Object = Driver.new_session(Plan.SCENARIO_ID)
	var autosaves: Array = []
	z.runner.autosaved.connect(func(r: Dictionary): autosaves.append([z.world().get_tick(), z.mode, r["ok"]]))
	var dz: Object = Driver.new(z)
	dz.open_device("door")
	z.skip_days(10)   # 열흘 맡기기: 자동 저장 간격(7일 = 168틱)을 지나요
	var refused: Dictionary = z.save_slot(SLOT)
	_ok("no_manual_save_while_away", not refused["ok"] and not z.can_save())
	dz.fast_forward()
	var during := autosaves.filter(func(x): return x[1] != CharSession.MODE_WORK)
	print("autosaves [tick, mode, ok] = %s" % [autosaves])
	_ok("autosave_deferred_until_wake", during.is_empty() and autosaves.size() == 1 and autosaves[0][2] and int(autosaves[0][0]) == z.world().get_tick(), str(autosaves))
	z.dispose()

	# ---- M6a 점검 화면 세이브 ↔ 캐릭터 화면 ----
	var r: Node = SimRunner.new()
	r.load_config()
	r.autosave_enabled = false
	r.start_new(Cfg.scenario("direct_play"))
	r.step(30)
	_ok("check_screen_saved", SaveStore.save(r, CHECK_SLOT)["ok"])
	var ec := _slot_entry(CHECK_SLOT)
	_ok("check_save_screen_is_check", SaveStore.screen_of(ec) == SaveStore.SCREEN_CHECK)
	var c: Object = CharSession.new()
	var lc: Dictionary = c.load_slot(CHECK_SLOT)
	var door_sp: Array = c.layout.stand_point_for_device(c.layout.device_by_id("door")) if lc["ok"] else [Vector2.ZERO, Vector2i.ZERO]
	_ok("m6a_save_without_character_loads_in_character_screen", lc["ok"] and c.world().state_hash() == r.world.state_hash() and c.pos == door_sp[0] and c.held.is_empty() and c.tool_index == 0, str(lc))
	var r2: Node = SimRunner.new()
	r2.load_config()
	var l2: Dictionary = SaveStore.load_into(r2, SLOT)
	_ok("character_save_loads_in_check_screen_runner", l2["ok"] and r2.world.get_tick() == saved.get("tick", -2) and r2.loaded_game.has("character"), str(l2))
	c.dispose()
	h.dispose()
	r.free()
	r2.free()
	U.finish(self, fails)
