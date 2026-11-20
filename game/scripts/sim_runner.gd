# SimRunner: SimWorld를 들고 배속·프레임당 틱 상한·자동 물 주기·세이브 묶음을 맡는 노드.
# 시뮬레이션 로직은 하나도 없어요(전부 C++ 쪽). 이 파일은 "언제 몇 틱을 부를지"만 정해요.
extends Node

const Cfg = preload("res://scripts/cfg.gd")

signal ticked                       # 한 프레임 분량의 틱을 돌린 뒤 (화면 갱신용)
signal command_rejected(message: String, command: Dictionary)
signal events_received(events: Array)
signal season_ended(summary: Dictionary)

var world: Object = null
var scenario_path := ""
var mode := ""
var speed_index := 0                # 0 = 일시정지, 1..4 = 배속 단계
var auto_irrigation := {"enabled": true, "hour": 8, "amount_mm": 4.0}
var season_ticks := 6312
var season_over_notified := false
var event_log: Array = []           # 개발자 패널용 최근 이벤트 (최대 N개)
var stats := {"ticks_run": 0, "frames": 0, "capped_frames": 0, "last_frame_ticks": 0}

var _accum := 0.0
var _ticks_per_second: Array = []
var _max_per_frame := 48
const EVENT_LOG_MAX := 300


func _ready() -> void:
	_ticks_per_second = Cfg.get_v("time", "ticks_per_second")
	_max_per_frame = int(Cfg.get_v("time", "max_ticks_per_frame"))
	season_ticks = int(Cfg.get_v("time", "season_ticks"))


# 새 판 시작. schedule_enabled=false면 직접 플레이.
func start_new(scenario: String, schedule_enabled: bool, auto_on: bool, mode_name: String) -> Dictionary:
	var w: Object = ClassDB.instantiate("SimWorld")
	var r: Dictionary = w.load_files(Cfg.get_v("start", "params"), scenario)
	if not r["ok"]:
		return r
	w.set_schedule_enabled(schedule_enabled)
	world = w
	scenario_path = scenario
	mode = mode_name
	auto_irrigation = {
		"enabled": auto_on,
		"hour": int(Cfg.get_v("auto_irrigation", "hour")),
		"amount_mm": float(Cfg.get_v("auto_irrigation", "amount_mm")),
	}
	_reset_runtime()
	return r


func _reset_runtime() -> void:
	speed_index = 0
	_accum = 0.0
	season_over_notified = false
	event_log.clear()


func set_speed(i: int) -> void:
	speed_index = clampi(i, 0, _ticks_per_second.size() - 1)


func toggle_pause(last_speed: int) -> void:
	set_speed(last_speed if speed_index == 0 else 0)


func ticks_per_second_for(i: int) -> float:
	return float(_ticks_per_second[i])


func _process(delta: float) -> void:
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
	stats["step_usec_sum"] = int(stats.get("step_usec_sum", 0)) + du
	stats["step_usec_max"] = maxi(int(stats.get("step_usec_max", 0)), du)
	ticked.emit()
	_check_season_end()


# n틱을 돌려요. 자동 물 주기는 정한 시각의 틱 직전에 밭 전체 irrigate를 하나 넣어요(m6-overview 6.5).
func step(n: int) -> bool:
	for i in range(n):
		if auto_irrigation["enabled"] and int(world.get_time()["hour"]) == int(auto_irrigation["hour"]):
			var cmd := {"action": "irrigate", "amount_mm": float(auto_irrigation["amount_mm"])}
			var er: Dictionary = world.enqueue(cmd)
			if not er["ok"]:
				command_rejected.emit("자동 물 주기가 거부됐어요: %s" % er["message"], cmd)
		var r: Dictionary = world.advance(1)
		if not r["ok"]:
			push_error("advance 실패: %s %s" % [r["code"], r["message"]])
			command_rejected.emit("시뮬레이션 오류: %s" % r["message"], {})
			speed_index = 0
			return false
		stats["ticks_run"] += 1
	stats["last_frame_ticks"] = n
	var ev: Array = world.take_events()
	if not ev.is_empty():
		event_log.append_array(ev)
		if event_log.size() > EVENT_LOG_MAX:
			event_log = event_log.slice(event_log.size() - EVENT_LOG_MAX)
		events_received.emit(ev)
	return true


func _check_season_end() -> void:
	if not season_over_notified and world.get_tick() >= season_ticks:
		season_over_notified = true
		speed_index = 0
		season_ended.emit({
			"tick": world.get_tick(),
			"harvest": world.get_harvest_totals(),
			"state_hash": world.state_hash(),  # 종료 시 한 번만
		})


# 플레이어 명령. 거부되면 알림 신호를 보내고 결과를 그대로 돌려줘요.
func submit(cmd: Dictionary) -> Dictionary:
	var r: Dictionary = world.enqueue(cmd)
	if not r["ok"]:
		command_rejected.emit(r["message"], cmd)
	return r


# 세이브 묶음에 들어가는 게임 쪽 구역 (코어 세이브 형식과 무관)
func game_state() -> Dictionary:
	return {
		"mode": mode,
		"scenario_path": scenario_path,
		"speed_index": speed_index,
		"auto_irrigation": auto_irrigation.duplicate(),
		"season_over_notified": season_over_notified,
	}


func restore_game_state(g: Dictionary) -> void:
	mode = str(g.get("mode", ""))
	scenario_path = str(g.get("scenario_path", ""))
	var a: Dictionary = g.get("auto_irrigation", {})
	auto_irrigation = {
		"enabled": bool(a.get("enabled", false)),
		"hour": int(a.get("hour", 8)),
		"amount_mm": float(a.get("amount_mm", 4.0)),
	}
	season_over_notified = bool(g.get("season_over_notified", false))
	speed_index = 0  # 불러온 뒤에는 일시정지로 시작 (프로토타입 판단)
	_accum = 0.0
	event_log.clear()
