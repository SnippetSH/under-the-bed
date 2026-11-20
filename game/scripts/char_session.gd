# 캐릭터 플레이 로직 층 (M6b 명세 4~8장). 그리기가 없어서 헤드리스 검사가 그대로 몰 수 있어요.
#  - 캐릭터 위치·방향·도구는 게임 쪽 상태예요(코어·해시에 안 들어가요).
#  - 시간: character_frame()은 캐릭터만 움직이고 시간을 흘리지 않아요. advance_clock()이 현실 시간 → 틱을 맡아요.
#    게임 화면은 매 프레임 둘 다 부르고, 검사는 "틱 기준"으로 따로 불러요(C5).
#  - 명령은 전부 M6a SimRunner를 거쳐요(submit / step. 자동 물 주기·자동 저장·작기 끝은 SimRunner 그대로, m6-overview 6.5·7장).
#    SimRunner는 트리에 달지 않아요(그래서 SimRunner 자신의 배속은 늘 0이고, 시간은 이 파일이 흘려요).
#  - 저장은 M6a 세이브 묶음(SaveStore) 그대로이고, 게임 구역에 "character"를 더해요(명세 8장, char_state()).
extends RefCounted

const Cfg = preload("res://scripts/cfg.gd")
const SimRunner = preload("res://scripts/sim_runner.gd")
const SaveStore = preload("res://scripts/save_store.gd")
const HouseLayout = preload("res://scripts/house_layout.gd")
const Tools = preload("res://scripts/char_tools.gd")
const RejectText = preload("res://scripts/reject_text.gd")

signal notice(text: String)
signal ticked
signal device_opened(device: Dictionary)
signal season_ended(summary: Dictionary)
signal mode_changed(mode: String)

const MODE_WORK := "work"
const MODE_SLEEP := "sleep"
const MODE_SKIP := "skip"
const CHAR_STATE_FORMAT := 1
const RAW_NOTES_MAX := 50

var runner: Node = null
var layout: HouseLayout = null
var catalog: Dictionary = {}
var cols := 20

# 캐릭터 (게임 쪽 상태)
var pos := Vector2.ZERO
var facing := Vector2i(0, 1)
var tool_index := 0
var tool_settings: Array = []
var walk_speed := 1.6
var radius := 0.22
var reach := 0.9
var moving := false                # 걷기 흔들림 꾸밈용 (저장 안 해요)
var running := false               # 달리는 중 (꾸밈·애니메이션용)
var acting := false                # 행동 키를 누르고 있는 중 (애니메이션 work)
var run_factor := 1.75
const MAX_SUBSTEP_M := 0.2         # 한 번에 걷는 최대 거리 (칸 1 m보다 충분히 작게, 명세 4장)
var raw_rejections: Array = []     # 거부 원문 기록 (개발자 패널 전용, 화면에는 한국어 문장만)
var walk_distance := 0.0

# 시간
var mode := MODE_WORK
var paused := false
var ui_blocked := false            # 장치 창·설정 창이 열린 동안 걷기·행동 막기 (시간은 흘러요)
var base_seconds_per_hour := 60.0  # ×1에서 게임 1시간 = 현실 N초 (설정 파일 [char_time])
var speed_multipliers: Array = [1.0]
var speed_index := 0               # 일하는 속도 단추 ×1~×4. 결과에 안 들어가요
var toolbar_shown := false         # 도구 줄을 Tab으로 고정해 보이는지 (화면 설정, 게임 쪽 저장)
var wake_hour := 6
var sleep_tps := 24.0
var skip_tps := 240.0
var max_per_frame := 48
var ff_target_tick := -1
var _clock_accum := 0.0
var _season_end_hit := false

# 행동
var _action_was_held := false
var _hold_cells := {}             # 한 번 누른 동안 이미 넣은 칸
var markers := {}                 # 칸 -> [도구 index...] "방금 한 일" 표시 (다음 틱에 지워요)
var stats := {"tool_commands": 0, "rejected": 0}
# 붙잡아 둔 행동(명세 6장): 플레이어 행동은 다음 틱을 돌리기 바로 전까지 여기 두고(취소 가능), 그때 한 순서대로 넣어요.
# 항목 {cmd, cell(-1 = 장치), tool(-1 = 장치), stroke}. 자동 물 주기는 붙잡지 않아요(같은 틱에서 플레이어 행동 다음, SimRunner.step 안에서 넣어요).
var held: Array = []
var _density := -1.0              # 재식 밀도(포기/m²). 정식 명령 기록 또는 시나리오 시작값에서 (모르면 -1)
var _stroke_id := 0
var tick_hook := Callable()       # 검사용: 틱마다 한 번 불러요(해시 모으기)


# 새 판. entry는 Cfg.scenario(id)와 같은 모양 {id, label, path, season_ticks, mode, auto_irrigation} (M6a SimRunner.start_new)
func start(entry: Dictionary) -> Dictionary:
	_make_runner()
	var r: Dictionary = runner.start_new(entry)
	if not r["ok"]:
		return r
	_after_world_ready()
	reset_character()
	tool_index = 0
	tool_settings = Tools.default_settings(catalog)
	speed_index = 0
	toolbar_shown = bool(Cfg.get_v("ui", "toolbar_default_shown"))
	return r


func _make_runner() -> void:
	if runner != null:
		runner.free()
	runner = SimRunner.new()
	runner.load_config()
	runner.extra_game_state = _game_section   # 메서드 연결이라 러너가 이 세션을 붙잡지 않아요
	runner.command_rejected.connect(_on_rejected)
	runner.season_ended.connect(_on_season_ended)
	runner.autosaved.connect(_on_autosaved)
	walk_speed = float(Cfg.get_v("character", "walk_speed_m_per_s"))
	radius = float(Cfg.get_v("character", "radius_m"))
	reach = float(Cfg.get_v("character", "reach_m"))
	run_factor = float(Cfg.get_v("character", "run_speed_factor"))
	base_seconds_per_hour = float(Cfg.get_v("char_time", "seconds_per_game_hour"))
	speed_multipliers = Cfg.get_v("char_time", "speed_multipliers")
	wake_hour = int(Cfg.get_v("char_time", "wake_hour"))
	sleep_tps = float(Cfg.get_v("char_time", "sleep_ticks_per_second"))
	skip_tps = float(Cfg.get_v("char_time", "skip_ticks_per_second"))
	max_per_frame = int(Cfg.get_v("time", "max_ticks_per_frame"))


# 거부: 원문은 개발자 기록에만, 화면에는 한국어 문장 (명세 2.3, 5.2)
func _on_rejected(message: String, cmd: Dictionary) -> void:
	stats["rejected"] += 1
	var t: int = runner.world.get_tick() if runner.world != null else -1
	_note_raw("tick %d %s: %s" % [t, str(cmd.get("action", "")), message])
	var hc: Dictionary = runner.world.get_house_controls() if runner.world != null else {}
	notice.emit(RejectText.korean(cmd, message, catalog, hc))


func _on_autosaved(res: Dictionary) -> void:
	if not res["ok"]:
		_note_raw("자동 저장 실패: %s" % res.get("message", ""))
		notice.emit("자동 저장을 하지 못했어요.")


func _note_raw(text: String) -> void:
	raw_rejections.append(text)
	if raw_rejections.size() > RAW_NOTES_MAX:
		raw_rejections.pop_front()


# 작기 끝 (명세 7.3): 정한 틱에서 멈추고(일시정지) 종료 화면을 띄워요. 자동 저장은 SimRunner가 해요
func _on_season_ended(summary: Dictionary) -> void:
	_season_end_hit = true
	_stop_fast_forward()
	paused = true
	season_ended.emit(summary)


func _after_world_ready() -> void:
	catalog = runner.world.get_catalog()
	var g: Vector2i = runner.world.get_grid_size()
	cols = g.x
	layout = HouseLayout.new()
	layout.setup(g.y, g.x, int(catalog["max_cover_layers"]))
	mode = MODE_WORK
	runner.autosave_hold = false
	paused = false
	ui_blocked = false
	ff_target_tick = -1
	_clock_accum = 0.0
	_season_end_hit = false
	_action_was_held = false
	_hold_cells.clear()
	held.clear()
	markers.clear()
	_stroke_id = 0
	_density = _find_density()


func reset_character() -> void:
	# 출입문 앞, 안쪽을 바라봐요
	var door: Dictionary = layout.device_by_id("door")
	var sp: Array = layout.stand_point_for_device(door)
	pos = sp[0]
	facing = Vector2i(0, 1)


func world() -> Object:
	return runner.world


func dispose() -> void:
	if runner != null:
		runner.extra_game_state = Callable()
		runner.free()
		runner = null


# ---------- 캐릭터 ----------
func can_act() -> bool:
	return mode == MODE_WORK and not paused and not ui_blocked


func target_cell() -> int:
	return layout.target_cell(pos, facing)


func device_in_front() -> Dictionary:
	return layout.device_in_front(pos, facing, reach)


# 한 프레임의 캐릭터 입력. move: x = 오른쪽(+x), y = 안쪽(+z), 각 성분 -1/0/1. 시간은 흘리지 않아요.
# run = Shift(달리기, 걷는 속도 × run_speed_factor). 게임 쪽만이고 코어·해시와 무관해요.
func character_frame(move: Vector2i, action_held: bool, dt: float, run := false) -> void:
	if not can_act():
		moving = false
		running = false
		acting = false
		_action_was_held = false
		_hold_cells.clear()
		return
	var pressed := action_held and not _action_was_held
	_action_was_held = action_held
	if pressed:
		_stroke_id += 1   # 한 번 누른 동안(누른 채 걷기)이 한 묶음
	acting = action_held
	if not action_held:
		_hold_cells.clear()
	moving = false
	running = false
	if action_held:
		_apply_hold()
	if move != Vector2i.ZERO:
		var before := pos
		# 행동 키를 누른 채 걸으면 바라보는 방향을 고정해요(옆 베드를 보며 통로를 따라 걷기, 명세 4장)
		if not action_held:
			facing = move
		var speed := walk_speed * (run_factor if run else 1.0)
		var total := Vector2(move).normalized() * speed * dt
		# 한 프레임에 많이 움직여도(달리기·낮은 프레임) 칸을 건너뛰지 않게 짧게 나눠 걸으며 칸마다 확인해요
		var n := maxi(1, int(ceil(total.length() / MAX_SUBSTEP_M)))
		for i in range(n):
			_walk(total / n)
			if action_held:
				_apply_hold()
		var moved := pos.distance_to(before)
		moving = moved > 0.0
		running = moving and run
		walk_distance += moved
	if pressed:
		var dev := device_in_front()
		if not dev.is_empty() and target_cell() < 0:
			device_opened.emit(dev)
			return


# 누른 채 걷기: 지금 대상 칸에 아직 안 넣었으면 한 번 넣어요
func _apply_hold() -> void:
	var c := target_cell()
	if c >= 0 and not _hold_cells.has(c):
		_hold_cells[c] = true
		use_tool_on(c)


func _walk(d: Vector2) -> void:
	# 축마다 따로 막아요(벽·베드를 따라 미끄러지기)
	var nx := pos + Vector2(d.x, 0.0)
	if layout.walkable(nx, radius):
		pos = nx
	var nz := pos + Vector2(0.0, d.y)
	if layout.walkable(nz, radius):
		pos = nz


func place(p: Vector2, f: Vector2i) -> void:
	pos = p
	facing = f


func select_tool(i: int) -> void:
	if i >= 0 and i < Tools.TOOLS.size():
		tool_index = i


func use_tool_on(cell: int) -> Dictionary:
	var cmd := Tools.build_command(tool_index, tool_settings[tool_index], cell, cols)
	var r := _check(cmd)
	if r["ok"]:
		stats["tool_commands"] += 1
		held.append({"cmd": cmd, "cell": cell, "tool": tool_index, "stroke": _stroke_id})
		_rebuild_markers()
	return r


# 행동하는 순간 판정 (바인딩 can_enqueue, 지금 상태 기준. 명세 5.2). 거부면 한국어 알림을 바로 띄우고 붙잡지 않아요
func _check(cmd: Dictionary) -> Dictionary:
	var r: Dictionary = runner.world.can_enqueue(cmd)
	if not r["ok"]:
		_on_rejected(str(r["message"]), cmd)
	return r


func _rebuild_markers() -> void:
	markers.clear()
	for e in held:
		var c: int = e["cell"]
		if c >= 0:
			if not markers.has(c):
				markers[c] = []
			markers[c].append(e["tool"])


# 재식 밀도 (포기/m²). 게임이 아는 값만 써요: 받아들여진 정식 명령(기록), 없으면 시나리오 파일의 [crop] 시작값. 모르면 -1
func plant_density() -> float:
	return _density


func _find_density() -> float:
	var log: Array = runner.world.get_command_log()
	for i in range(log.size() - 1, -1, -1):
		var c: Dictionary = log[i]["command"]
		if str(c.get("action", "")) == "plant":
			return float(c.get("plants_per_m2", -1.0))
	var path := str(Cfg.scenario(runner.scenario_id).get("path", ""))
	if path == "" or not FileAccess.file_exists(path):
		return -1.0
	var in_crop := false
	for raw in FileAccess.get_file_as_string(path).split("\n"):
		var line := raw.strip_edges()
		if line.begins_with("["):
			in_crop = line.begins_with("[crop]")
		elif in_crop and line.begins_with("plants_per_m2"):
			var val := line.split("=", false, 1)
			if val.size() == 2:
				return float(val[1].split("#")[0].strip_edges())
	return -1.0


# 정식 미리 보기(명세 6장, R18): 붙잡아 둔 행동에 정식이 있으면 (취소하거나 넣을 때 거부되면 사라져요)
func has_held_plant() -> bool:
	for e in held:
		if str(e["cmd"].get("action", "")) == "plant":
			return true
	return false


func held_count() -> int:
	return held.size()


# Z: 가장 최근 행동 하나를 취소. 돌려준 항목이 비어 있으면 취소할 것이 없어요
func cancel_last() -> Dictionary:
	if held.is_empty():
		return {}
	var e: Dictionary = held.pop_back()
	_rebuild_markers()
	_hold_cells.erase(e["cell"])
	return e


# Shift+Z: 마지막으로 한 묶음(한 번 누른 채 걸으며 한 행동들, 또는 장치 행동 하나)을 한꺼번에 취소
func cancel_last_stroke() -> Array:
	if held.is_empty():
		return []
	var sid: int = held[-1]["stroke"]
	var out := []
	while not held.is_empty() and held[-1]["stroke"] == sid:
		out.push_front(held.pop_back())
	_rebuild_markers()
	return out


# 다음 틱을 돌리기 바로 전: 붙잡아 둔 행동을 한 순서대로 넣어요. 그때 거부되면 SimRunner 신호로 한국어 알림이 떠요
func flush_held() -> void:
	if held.is_empty():
		return
	var list := held
	held = []
	for e in list:
		var r: Dictionary = runner.submit(e["cmd"])
		if r["ok"] and str(e["cmd"].get("action", "")) == "plant":
			_density = float(e["cmd"].get("plants_per_m2", _density))
	markers.clear()


# 장치 조작 창에서 부르는 명령 (자동 물 주기는 게임 쪽 설정)
func device_command(cmd: Dictionary) -> Dictionary:
	var r := _check(cmd)
	if r["ok"]:
		_stroke_id += 1
		held.append({"cmd": cmd, "cell": -1, "tool": -1, "stroke": _stroke_id})
		notice.emit("다음 1시간이 시작될 때 적용돼요. 그 전에는 Z로 취소할 수 있어요.")
	return r


# 자동 물 주기 조절기 (M6a SimRunner 설정 그대로, m6-overview 6.5)
func set_auto_irrigation(enabled: bool, hour: int, amount_mm: float) -> void:
	runner.set_auto_irrigation(enabled, hour, amount_mm)


# ---------- 시간 ----------
func hour_now() -> int:
	return int(runner.world.get_time()["hour"])


# 지금 일하는 속도에서 게임 1시간 = 현실 몇 초
func hour_seconds() -> float:
	return base_seconds_per_hour / float(speed_multipliers[speed_index])


func speed_multiplier() -> float:
	return float(speed_multipliers[speed_index])


func set_speed(i: int) -> void:
	speed_index = clampi(i, 0, speed_multipliers.size() - 1)


# 지금 시간 안에서 흐른 몫 (0~1, 시계 바늘 꾸밈용)
func hour_fraction() -> float:
	return _clock_accum if mode == MODE_WORK else 0.0


func can_skip_hour() -> bool:
	return mode == MODE_WORK and not paused


# "1시간 건너뛰기": 다음 시간이 시작되는 틱을 지금 돌려요. 대기 중인 행동은 그 틱에 평소대로 적용돼요
func skip_hour() -> bool:
	if not can_skip_hour():
		return false
	_clock_accum = 0.0
	step_ticks(1)
	return true


# 다음 기상 시각 틱 (지금이 기상 시각이면 하루 뒤)
func next_wake_tick() -> int:
	var t: int = runner.world.get_tick()
	var d := posmod(wake_hour - hour_now(), 24)
	return t + (24 if d == 0 else d)


func go_to_sleep() -> bool:
	return _begin_fast_forward(MODE_SLEEP, next_wake_tick())


func skip_days(days: int) -> bool:
	days = clampi(days, 1, int(Cfg.get_v("char_time", "skip_days_max")))
	return _begin_fast_forward(MODE_SKIP, next_wake_tick() + 24 * (days - 1))


# 며칠 맡기기 도중 아무 키: 다음 기상 시각에 멈춰요
func interrupt_skip() -> void:
	if mode == MODE_SKIP:
		ff_target_tick = mini(ff_target_tick, next_wake_tick())
		notice.emit("다음 기상 시각에 멈춰요.")


func _begin_fast_forward(m: String, target: int) -> bool:
	if mode != MODE_WORK or paused:
		return false
	mode = m
	ff_target_tick = target
	_clock_accum = 0.0
	_action_was_held = false
	_hold_cells.clear()
	runner.autosave_hold = true   # 자는 동안에는 저장하지 않아요. 멈춘 뒤에 저장해요 (명세 8장)
	mode_changed.emit(mode)
	return true


func _stop_fast_forward() -> void:
	if mode != MODE_WORK:
		mode = MODE_WORK
		ff_target_tick = -1
		_clock_accum = 0.0
		runner.release_autosave_hold()
		mode_changed.emit(mode)


func remaining_ff_ticks() -> int:
	return maxi(0, ff_target_tick - runner.world.get_tick()) if mode != MODE_WORK else 0


# 현실 시간 dt초를 흘려요. 돌린 틱 수를 돌려줘요.
func advance_clock(dt: float) -> int:
	if paused:
		return 0
	var n := 0
	if mode == MODE_WORK:
		_clock_accum += dt / hour_seconds()
		n = int(_clock_accum)
		_clock_accum -= n
	else:
		_clock_accum += dt * (sleep_tps if mode == MODE_SLEEP else skip_tps)
		n = int(_clock_accum)
		_clock_accum -= n
		n = mini(n, remaining_ff_ticks())
	if n > max_per_frame:
		n = max_per_frame   # 상한: 실제 속도가 느려질 뿐 결과는 같아요
		_clock_accum = 0.0
	var ran := 0
	if n > 0:
		ran = step_ticks(n)
	if mode != MODE_WORK and runner.world.get_tick() >= ff_target_tick:
		_stop_fast_forward()
		notice.emit("일어났어요 (%02d시)." % hour_now())
	return ran


# n틱을 돌려요(작기 끝에 닿으면 그 틱에서 멈춰요). 돌린 틱 수를 돌려줘요.
func step_ticks(n: int) -> int:
	var ran := 0
	for i in range(n):
		flush_held()   # 시간을 흘리는 모든 길(일하는 시간, 1시간 건너뛰기, 자러 가기, 며칠 맡기기)이 여기를 지나요
		_season_end_hit = false
		if not runner.step(1):
			_stop_fast_forward()
			paused = true   # 시뮬레이션 오류: 멈춰요 (SimRunner가 알림 신호를 냈어요)
			break
		ran += 1
		markers.clear()   # 다음 시간이 시작되면 "방금 한 일" 표시를 지우고 코어 상태대로 그려요 (명세 6장)
		if tick_hook.is_valid():
			tick_hook.call()
		if _season_end_hit:
			break   # 작기 끝: 종료 화면의 해시가 정확히 그 틱의 것이 되게 (명세 7.3)
	ticked.emit()
	return ran


# ---------- 저장 (명세 8장) ----------
# 세이브 묶음 게임 구역의 "character" (SimRunner.extra_game_state가 불러요). 모양은 save_store.gd 머리 주석
func char_state() -> Dictionary:
	return {
		"format": CHAR_STATE_FORMAT,
		"pos": [pos.x, pos.y],
		"facing": [facing.x, facing.y],
		"tool_index": tool_index,
		"tool_settings": tool_settings.duplicate(true),
		"speed_index": speed_index,
		"toolbar_shown": toolbar_shown,
		"held": held.map(func(e): return {"cmd": e["cmd"].duplicate(true), "cell": e["cell"], "tool": e["tool"], "stroke": e["stroke"]}),
	}


func _game_section() -> Dictionary:
	return {"character": char_state()}


# 없는 키(M6a 세이브, 옛 형식)는 기본값으로 둬요
func restore_char_state(c: Dictionary) -> void:
	var p: Array = c.get("pos", [pos.x, pos.y])
	pos = Vector2(float(p[0]), float(p[1]))
	var f: Array = c.get("facing", [facing.x, facing.y])
	facing = Vector2i(int(f[0]), int(f[1]))
	tool_index = clampi(int(c.get("tool_index", 0)), 0, Tools.TOOLS.size() - 1)
	set_speed(int(c.get("speed_index", 0)))
	held = []
	for e in c.get("held", []):
		held.append({"cmd": _normalize_cmd(e.get("cmd", {})), "cell": int(e.get("cell", -1)), "tool": int(e.get("tool", -1)), "stroke": int(e.get("stroke", 0))})
	_stroke_id = held[-1]["stroke"] if not held.is_empty() else 0
	_rebuild_markers()
	toolbar_shown = bool(c.get("toolbar_shown", Cfg.get_v("ui", "toolbar_default_shown")))
	var ts: Array = c.get("tool_settings", [])
	var defs := Tools.default_settings(catalog)
	tool_settings = []
	for i in range(Tools.TOOLS.size()):
		var s: Dictionary = defs[i]
		if i < ts.size() and typeof(ts[i]) == TYPE_DICTIONARY:
			if s.has("kind") and ts[i].has("kind"):
				s["kind"] = str(ts[i]["kind"])
			if s.has("amount") and ts[i].has("amount"):
				s["amount"] = float(ts[i]["amount"])
		tool_settings.append(s)


# JSON을 거치면 정수가 실수가 돼요. 바인딩은 정수 자리(area, layer, layers)에 실수를 받지 않아서 되돌려요
static func _normalize_cmd(cmd: Dictionary) -> Dictionary:
	var out := cmd.duplicate(true)
	if out.has("area"):
		out["area"] = Array(out["area"]).map(func(v): return int(v))
	for k in ["layer", "layers"]:
		if out.has(k):
			out[k] = int(out[k])
	for k in ["on"]:
		if out.has(k):
			out[k] = bool(out[k])
	return out


func can_save() -> bool:
	return mode == MODE_WORK   # 자러 가기·며칠 맡기기 도중에는 저장하지 않아요


# 저장 칸 id(SaveStore: slot_N, quick, auto)에 지금 판을 저장해요. 결과는 SaveStore.save와 같아요
func save_slot(id: String = SaveStore.QUICK) -> Dictionary:
	if not can_save():
		return {"ok": false, "message": "자는 동안에는 저장할 수 없어요", "path": "", "preserved": ""}
	return SaveStore.save(runner, id)


# 저장 칸을 불러와요. 실패하면 지금 판 그대로예요. M6a 점검 화면 세이브("character" 없음)도 읽어요(캐릭터는 출입문 앞)
func load_slot(id: String = SaveStore.QUICK) -> Dictionary:
	if runner == null:
		_make_runner()
	var r: Dictionary = SaveStore.load_into(runner, id)
	if not r["ok"]:
		return r
	_after_world_ready()
	reset_character()
	tool_index = 0
	speed_index = 0
	tool_settings = Tools.default_settings(catalog)
	restore_char_state(runner.loaded_game.get("character", {}))
	return r
