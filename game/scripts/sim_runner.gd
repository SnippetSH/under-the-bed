# SimRunner: SimWorld를 들고 배속·프레임당 틱 상한·자동 물 주기·자동 저장·작기 종료를 맡는 노드.
# 시뮬레이션 로직은 하나도 없어요(전부 C++ 쪽). 이 파일은 "언제 몇 틱을 부를지"와 "어떤 명령을 넣을지"만 정해요.
# 결과를 정하는 것은 "어느 틱 직전에 어떤 명령을 어떤 순서로 넣었는가"뿐이에요 (m6-overview 3.3).
extends Node

const Cfg = preload("res://scripts/cfg.gd")
const SaveStore = preload("res://scripts/save_store.gd")

signal ticked                                       # 한 프레임 분량의 틱을 돌린 뒤 (화면 갱신용)
signal command_rejected(message: String, command: Dictionary)
signal events_received(events: Array)
signal season_ended(summary: Dictionary)
signal autosaved(result: Dictionary)                # 자동 저장을 시도한 뒤 (실패 안내용)
signal world_replaced                               # 새 판을 시작하거나 세이브를 불러온 뒤

const EVENT_LOG_MAX := 300
const GAME_STATE_FORMAT := 1

var world: Object = null
var scenario_id := ""
var scenario_label := ""
var mode := ""
var season_ticks := 0                               # 0이면 작기 종료 없음
var season_over_notified := false
var speed_index := 0                                # 0 = 일시정지, 1.. = 배속 단계
var auto_irrigation := {"enabled": false, "hour": 8, "amount_mm": 4.0}
var autosave_enabled := true
# M6b 캐릭터 화면: 자러 가기·며칠 맡기기 동안은 자동 저장을 미뤘다가 멈춘 뒤에 해요(m6b-character-play 8장).
# true인 동안 저장할 때가 오면 기억만 해 두고, release_autosave_hold()가 그때 한 번 저장해요. 점검 화면은 늘 false예요.
var autosave_hold := false
# M6b: 세이브 묶음 게임 구역에 더할 화면 쪽 값 (캐릭터 화면이 {"character": …}를 돌려주는 Callable을 넣어요). 비어 있으면 없음
var extra_game_state := Callable()
# 마지막으로 불러온 세이브의 게임 구역 전체 (캐릭터 화면이 "character"를 되살릴 때 읽어요). 새 판이면 빈 사전
var loaded_game: Dictionary = {}
var event_log: Array = []                           # 개발자 패널용 최근 이벤트 (최대 EVENT_LOG_MAX개)
var stats := {"ticks_run": 0, "frames": 0, "capped_frames": 0, "step_usec_sum": 0, "step_usec_max": 0}

var _accum := 0.0
var _last_auto_day := -1                            # 자동 물 주기를 마지막으로 넣은 게임 날짜(elapsed_days). 하루 한 번만
var _ticks_per_second: Array = []
var _max_per_frame := 48
var _autosave_ticks := 168
var _autosave_pending := false


func _ready() -> void:
	load_config()


# 설정 읽기. 노드를 트리에 달지 않고 쓰는 검사 스크립트는 이걸 직접 불러요.
func load_config() -> void:
	_ticks_per_second = Cfg.get_v("time", "ticks_per_second")
	_max_per_frame = int(Cfg.get_v("time", "max_ticks_per_frame"))
	_autosave_ticks = int(Cfg.get_v("time", "autosave_interval_days")) * 24


# ---------- 판 시작·불러오기 ----------
# 새 판. entry는 Cfg.scenario(id)와 같은 모양 {id, label, path, season_ticks, mode, auto_irrigation}.
func start_new(entry: Dictionary) -> Dictionary:
	var w: Object = SimWorld.new()
	var r: Dictionary = w.load_files(Cfg.get_v("start", "params"), entry["path"])
	if not r["ok"]:
		return r
	w.set_schedule_enabled(entry["mode"] == "watch")
	world = w
	scenario_id = str(entry["id"])
	scenario_label = str(entry["label"])
	mode = str(entry["mode"])
	season_ticks = int(entry["season_ticks"])
	auto_irrigation = {
		"enabled": bool(entry["auto_irrigation"]),
		"hour": int(Cfg.get_v("auto_irrigation", "hour")),
		"amount_mm": float(Cfg.get_v("auto_irrigation", "amount_mm")),
	}
	_last_auto_day = -1
	season_over_notified = false
	loaded_game = {}
	_reset_runtime()
	world_replaced.emit()
	return r


# SaveStore.load_into가 읽기에 성공한 뒤 불러요. 불러온 직후는 일시정지예요.
func adopt_loaded(new_world: Object, game: Dictionary) -> void:
	world = new_world
	mode = str(game.get("mode", ""))
	scenario_id = str(game.get("scenario_id", ""))
	scenario_label = str(game.get("scenario_label", ""))
	season_ticks = int(game.get("season_ticks", 0))
	var a: Dictionary = game.get("auto_irrigation", {})
	auto_irrigation = {
		"enabled": bool(a.get("enabled", false)),
		"hour": int(a.get("hour", 8)),
		"amount_mm": float(a.get("amount_mm", 4.0)),
	}
	_last_auto_day = int(game.get("last_auto_day", -1))
	season_over_notified = bool(game.get("season_over_notified", false))
	loaded_game = game.duplicate(true)
	_reset_runtime()
	world_replaced.emit()


func _reset_runtime() -> void:
	speed_index = 0
	_accum = 0.0
	_autosave_pending = false
	event_log.clear()


# 세이브 묶음의 게임 구역 (코어 세이브 형식과 무관). 배속은 저장하지 않아요 (불러오면 일시정지).
# 캐릭터 화면이면 extra_game_state가 돌려준 키(예: "character")가 더해져요. 그 키가 없는 옛 세이브도 그대로 읽혀요.
func game_state() -> Dictionary:
	var g := {
		"format": GAME_STATE_FORMAT,
		"mode": mode,
		"scenario_id": scenario_id,
		"scenario_label": scenario_label,
		"season_ticks": season_ticks,
		"auto_irrigation": auto_irrigation.duplicate(),
		"last_auto_day": _last_auto_day,
		"season_over_notified": season_over_notified,
	}
	if extra_game_state.is_valid():
		g.merge(extra_game_state.call(), true)
	return g


# ---------- 배속 ----------
func set_speed(i: int) -> void:
	speed_index = clampi(i, 0, _ticks_per_second.size() - 1)


func ticks_per_second_for(i: int) -> float:
	return float(_ticks_per_second[i])


func speed_count() -> int:
	return _ticks_per_second.size()


func _process(delta: float) -> void:
	tick_frame(delta)


# 한 프레임: 쌓인 시간만큼 틱을 돌려요 (상한 있음). 화면 갱신 신호는 ticked.
func tick_frame(delta: float) -> void:
	if world == null or speed_index == 0:
		return
	_accum += delta * float(_ticks_per_second[speed_index])
	var n := int(_accum)
	if n <= 0:
		return
	_accum -= n
	stats["frames"] += 1
	var t0 := Time.get_ticks_usec()
	if n > _max_per_frame:
		# 상한: 못 돌린 틱은 버려요(밀리면 따라잡느라 더 멈추므로). 배속이 실제로 느려질 뿐 결과는 같아요
		n = _max_per_frame
		_accum = 0.0
		stats["capped_frames"] += 1
	step(n)
	var du := Time.get_ticks_usec() - t0
	stats["step_usec_sum"] += du
	stats["step_usec_max"] = maxi(int(stats["step_usec_max"]), du)
	ticked.emit()


# ---------- 틱 돌리기 ----------
# n틱을 돌려요. 틱마다 (자동 물 주기) -> advance(1) -> (자동 저장) -> (작기 끝이면 멈춤).
# 작기 끝에 닿으면 n틱이 남아 있어도 그 틱에서 멈추고 일시정지해요(종료 화면의 해시가 정확히 그 틱의 것이 되게).
# 돌아오는 값: 시뮬레이션 오류 없이 끝났는지.
func step(n: int) -> bool:
	var ok := true
	for i in range(n):
		_auto_irrigate_if_due()
		var r: Dictionary = world.advance(1)
		if not r["ok"]:
			push_error("advance 실패: %s %s" % [r["code"], r["message"]])
			command_rejected.emit("시뮬레이션 오류: %s" % r["message"], {})
			speed_index = 0
			ok = false
			break
		stats["ticks_run"] += 1
		var tick: int = world.get_tick()
		var season_reached := season_ticks > 0 and not season_over_notified and tick >= season_ticks
		if autosave_enabled and not season_reached and tick % _autosave_ticks == 0:
			_autosave()
		if season_reached:
			_end_season()
			break
	var ev: Array = world.take_events()
	if not ev.is_empty():
		event_log.append_array(ev)
		if event_log.size() > EVENT_LOG_MAX:
			event_log = event_log.slice(event_log.size() - EVENT_LOG_MAX)
		events_received.emit(ev)
	return ok


# 자동 물 주기 (m6-overview 6.5, 결정 F3): 설정이 켜져 있고 다음에 처리할 틱의 시각이 정한 시각이고
# 오늘 아직 안 넣었으면, 그 틱 직전에 밭 전체 irrigate를 플레이어 명령으로 하나 넣어요.
# 설정은 틱마다 읽으므로 바꾸면 바로 따라요: 오늘 정한 시각이 아직 안 지났으면 오늘부터예요.
func _auto_irrigate_if_due() -> void:
	if not auto_irrigation["enabled"]:
		return
	var t: Dictionary = world.get_time()
	if int(t["hour"]) != int(auto_irrigation["hour"]) or int(t["elapsed_days"]) == _last_auto_day:
		return
	_last_auto_day = int(t["elapsed_days"])
	var cmd := {"action": "irrigate", "amount_mm": float(auto_irrigation["amount_mm"])}
	var r: Dictionary = world.enqueue(cmd)
	if not r["ok"]:
		command_rejected.emit("자동 물 주기가 거부됐어요: %s" % r["message"], cmd)


func set_auto_irrigation(enabled: bool, hour: int, amount_mm: float) -> void:
	auto_irrigation = {"enabled": enabled, "hour": clampi(hour, 0, 23), "amount_mm": amount_mm}


func _autosave() -> void:
	if autosave_hold:
		_autosave_pending = true
		return
	autosaved.emit(SaveStore.save(self, SaveStore.AUTO))


# 미뤄 둔 자동 저장이 있으면 지금 해요 (캐릭터 화면: 자러 가기·며칠 맡기기가 멈춘 뒤)
func release_autosave_hold() -> void:
	autosave_hold = false
	if _autosave_pending:
		_autosave_pending = false
		if autosave_enabled:
			_autosave()


func _end_season() -> void:
	season_over_notified = true
	speed_index = 0
	if autosave_enabled:
		_autosave()
	season_ended.emit({
		"tick": world.get_tick(),
		"time": world.get_time(),
		"harvest": world.get_harvest_totals(),
		"state_hash": world.state_hash(),  # 종료 시 한 번만 (비싸요)
	})


# 플레이어 명령. 거부되면 알림 신호를 보내고 결과를 그대로 돌려줘요.
func submit(cmd: Dictionary) -> Dictionary:
	var r: Dictionary = world.enqueue(cmd)
	if not r["ok"]:
		command_rejected.emit(str(r["message"]), cmd)
	return r
