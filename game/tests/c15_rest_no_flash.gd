# C15 (디렉터 요청 2026-11-20, 광과민 대응): 자러 가기·며칠 맡기기가 깜빡이지 않아요.
#  빛(DayLight)은 시계 대신 "시작 빛 → 밤 → 기상 빛"을 딱 한 번 천천히 보여 주고, 틱은 그 시간에 고르게 나눠 돌아요.
#  A) 30일 맡기기 / 자러 가기 / 맡기기 도중 끊기: 프레임마다 보이는 밝기를 모아서
#     - 방향 바뀜 2번 이하(어두워졌다 밝아지는 한 번), 가장 어두운 값 = 밤 빛
#     - 밝기 변화 속도 ≤ 8 × (밝기 폭) / 표현 길이 그리고 ≤ 1.0 /초 (설정과 무관한 상한. 예전 방식은 하루가 0.1초라 수십 /초)
#     - 표현과 틱이 함께 끝나고(표현 길이 안에서), 끝난 뒤 시계 빛으로 이어질 때 튀지 않음
#     - 마지막 틱·해시 = 같은 틱 수를 바로 돌린 판 (자동 물 주기 포함 명령 기록 길이도 같음)
#  B) 작기 끝이 맡기기 도중에 오면: 그 틱에서 멈추고 빛은 settle 동안 부드럽게 시계 빛으로 (튐 없음)
#  C) 실제 캐릭터 화면(char_play) 프레임마다 presented_light_level(): 자러 가기 한 번이 한 번만 어두워졌다 밝아짐
#   godot --headless --path game --script res://tests/c15_rest_no_flash.gd -- out=<폴더>
extends SceneTree

const U = preload("res://tests/util.gd")
const Driver = preload("res://tests/char_driver.gd")
const DayLight = preload("res://scripts/day_light.gd")
const Session = preload("res://scripts/session.gd")
const SaveStore = preload("res://scripts/save_store.gd")

const SCENARIO := "char_planted"
const DT := 1.0 / 60.0
const EPS := 1e-4                 # 이보다 작은 차이는 평평한 것으로 봐요
const RATE_K := 8.0               # 밝기 변화 속도 상한 = RATE_K × 폭 / 표현 길이
const RATE_ABS_PER_S := 1.0       # 절대 상한 (밝기 0~1을 1초보다 빨리 바꾸지 않음)
const START_HOUR := 14            # 밝은 낮에 시작해야 어두워지는 것이 보여요

var fails := PackedStringArray()


func _ok(name: String, cond: bool, detail := "") -> void:
	if not cond:
		fails.push_back("%s %s" % [name, detail])
	print("%s %s %s" % ["ok  " if cond else "FAIL", name, detail])


# 값 목록의 방향 바뀜 수 (EPS보다 작은 변화는 무시)
static func direction_changes(xs: Array) -> int:
	var n := 0
	var last_dir := 0
	var ref: float = xs[0]
	for x in xs:
		var d: float = x - ref
		if absf(d) < EPS:
			continue
		var dir := 1 if d > 0.0 else -1
		if last_dir != 0 and dir != last_dir:
			n += 1
		last_dir = dir
		ref = x
	return n


static func max_rate(xs: Array, dt: float) -> float:
	var m := 0.0
	for i in range(1, xs.size()):
		m = maxf(m, absf(float(xs[i]) - float(xs[i - 1])) / dt)
	return m


func _to_hour(s: Object, h: int) -> void:
	while s.hour_now() != h:
		s.step_ticks(1)


# 한 번 돌리기. kind = "sleep" | "skip", interrupt_ticks > 0이면 그만큼 돈 뒤 아무 키
func run_case(label: String, kind: String, days: int, interrupt_ticks: int) -> void:
	var s: Object = Driver.new_session(SCENARIO)
	var ref: Object = Driver.new_session(SCENARIO)
	_to_hour(s, START_HOUR)
	_to_hour(ref, START_HOUR)
	var light := DayLight.new()
	for i in range(3):
		light.update(s, DT)        # 일하는 동안의 빛(시작 빛)을 잡아 둬요
	var start_level := light.last_level()
	var t0: int = s.world().get_tick()
	var ok_begin: bool = s.go_to_sleep() if kind == "sleep" else s.skip_days(days)
	var duration: float = s.sleep_cycle_s if kind == "sleep" else s.skip_cycle_s
	var levels := [start_level]
	var energies := [float(light.last()["sun_energy_factor"])]
	var icons := 0
	var last_icon: bool = light.last()["day_icon"]
	var frames := 0
	var interrupted := false
	while s.mode != "work" and frames < 100000:
		s.advance_clock(DT)
		var lp: Dictionary = light.update(s, DT)
		frames += 1
		levels.append(DayLight.level(lp))
		energies.append(float(lp["sun_energy_factor"]))
		if bool(lp["day_icon"]) != last_icon:
			icons += 1
			last_icon = lp["day_icon"]
		if interrupt_ticks > 0 and not interrupted and s.world().get_tick() - t0 >= interrupt_ticks:
			interrupted = true
			s.interrupt_skip()
	var ran: int = s.world().get_tick() - t0
	var end_frame := levels.size()
	# 끝난 뒤 1초 더 (시계 빛으로 이어짐)
	for i in range(60):
		s.advance_clock(DT)
		var lp2: Dictionary = light.update(s, DT)
		levels.append(DayLight.level(lp2))
		energies.append(float(lp2["sun_energy_factor"]))
	var lo: float = levels.min()
	var hi: float = levels.max()
	var span := hi - lo
	var night_level := DayLight.level(DayLight.params(DayLight.NIGHT_HOUR))
	var wake_level := DayLight.level(DayLight.params(float(s.wake_hour)))
	var bound := minf(RATE_K * span / duration, RATE_ABS_PER_S)
	var rate := max_rate(levels, DT)
	var e_span: float = energies.max() - energies.min()
	var e_rate := max_rate(energies, DT)
	print("%s: ticks=%d frames=%d (%.2f s, cycle %.1f s) level start=%.3f min=%.3f night=%.3f end=%.3f wake=%.3f max_rate=%.3f/s bound=%.3f/s sun_rate=%.3f/s icon_swaps=%d" % [
		label, ran, frames, frames * DT, duration, start_level, lo, night_level, levels[end_frame - 1], wake_level, rate, bound, e_rate, icons])
	_ok("%s_started" % label, ok_begin)
	_ok("%s_one_dip_direction_changes_le_2" % label, direction_changes(levels) <= 2 and direction_changes(energies) <= 2,
			"level=%d sun=%d" % [direction_changes(levels), direction_changes(energies)])
	_ok("%s_dips_to_night_once" % label, absf(lo - night_level) < EPS and start_level - lo > 0.1)
	_ok("%s_rate_bounded" % label, rate <= bound + EPS and e_rate <= minf(RATE_K * e_span / duration, RATE_ABS_PER_S * 2.0) + EPS,
			"level %.3f <= %.3f" % [rate, bound])
	_ok("%s_cycle_and_ticks_end_together" % label, absf(frames * DT - duration) <= DT * 1.5, "%.3f s vs %.1f s" % [frames * DT, duration])
	_ok("%s_ends_on_wake_light_no_jump" % label, absf(float(levels[end_frame - 1]) - wake_level) < EPS and max_rate(levels.slice(end_frame - 1), DT) <= bound + EPS)
	_ok("%s_icon_swaps_le_2" % label, icons <= 2, str(icons))
	_ok("%s_stops_at_wake_hour" % label, s.hour_now() == s.wake_hour)
	if interrupt_ticks > 0:
		_ok("%s_interrupt_stops_at_next_wake" % label, ran > interrupt_ticks and ran <= interrupt_ticks + 24 and ran < days * 24, "ran=%d" % ran)
	# 같은 틱 수를 바로 돌린 판과 같아요 (예전 속도 방식과 같은 틱·순서)
	ref.step_ticks(ran)
	_ok("%s_same_tick_and_hash_as_direct_advance" % label, ref.world().get_tick() == t0 + ran
			and _hash_at(s, t0 + ran) == ref.world().state_hash() and _log_len_at(s, t0 + ran) == ref.world().get_command_log().size(),
			"%s vs %s" % [_hash_at(s, t0 + ran), ref.world().state_hash()])
	s.dispose()
	ref.dispose()


# 끝난 뒤 1초 더 흘린 시계(1분 남짓)는 틱을 넘기지 않아요. 넘겼다면 비교가 틀리니 그대로 알려요
func _hash_at(s: Object, tick: int) -> String:
	return s.world().state_hash() if s.world().get_tick() == tick else "tick moved to %d" % s.world().get_tick()


func _log_len_at(s: Object, tick: int) -> int:
	return s.world().get_command_log().size() if s.world().get_tick() == tick else -1


var _ended: Array = []


func _on_season_ended(_sum: Dictionary, s: Object) -> void:
	_ended.append(s.world().get_tick())


func season_end_case() -> void:
	var s: Object = Driver.new_session(SCENARIO)
	_to_hour(s, START_HOUR)
	var light := DayLight.new()
	for i in range(3):
		light.update(s, DT)
	var end_tick: int = s.world().get_tick() + 50
	s.runner.season_ticks = end_tick
	_ended.clear()
	s.season_ended.connect(_on_season_ended.bind(s))   # 메서드 연결 (람다는 세션을 붙잡아 서로 붙잡게 돼요)
	var ended := _ended
	s.skip_days(10)
	var levels := [light.last_level()]
	var frames := 0
	while s.mode != "work" and frames < 100000:
		s.advance_clock(DT)
		levels.append(DayLight.level(light.update(s, DT)))
		frames += 1
	var stop_frame := levels.size()
	for i in range(int(2.0 / DT)):
		s.advance_clock(DT)
		levels.append(DayLight.level(light.update(s, DT)))
	var clock_level := DayLight.level(DayLight.params(float(s.hour_now()) + s.hour_fraction()))
	var rate := max_rate(levels, DT)
	print("season_end: stopped tick=%d (end %d) after %.2f s, paused=%s, level at stop=%.3f -> %.3f (clock %.3f) max_rate=%.3f/s" % [
		s.world().get_tick(), end_tick, frames * DT, s.paused, levels[stop_frame - 1], levels[-1], clock_level, rate])
	_ok("season_end_mid_skip_stops_at_end_tick", ended.size() == 1 and int(ended[0]) == end_tick and s.world().get_tick() == end_tick and s.paused)
	_ok("season_end_light_settles_to_clock_without_jump", absf(float(levels[-1]) - clock_level) < EPS and rate <= RATE_ABS_PER_S + EPS
			and direction_changes(levels) <= 2, "rate=%.3f changes=%d" % [rate, direction_changes(levels)])
	s.dispose()


func scene_case() -> void:
	Session.choose_scenario(SCENARIO)
	var cp: Node = load("res://scenes/char_play.tscn").instantiate()
	get_root().add_child(cp)
	for i in range(3):
		await process_frame
	var s: Object = cp.s
	_to_hour(s, START_HOUR)
	for i in range(3):
		await process_frame
	var levels := [cp.presented_light_level()]
	var times := [Time.get_ticks_usec()]
	s.go_to_sleep()
	var guard := 0
	while s.mode != "work" and guard < 100000:
		await process_frame
		levels.append(cp.presented_light_level())
		times.append(Time.get_ticks_usec())
		guard += 1
	for i in range(10):
		await process_frame
		levels.append(cp.presented_light_level())
		times.append(Time.get_ticks_usec())
	var worst := 0.0
	for i in range(1, levels.size()):
		var dt: float = maxf((times[i] - times[i - 1]) / 1e6, 1e-3)
		worst = maxf(worst, absf(float(levels[i]) - float(levels[i - 1])) / dt)
	var wall: float = (times[-1] - times[0]) / 1e6
	print("scene sleep: frames=%d wall=%.2f s level min=%.3f max=%.3f changes=%d worst_rate=%.3f/s" % [
		levels.size(), wall, levels.min(), levels.max(), direction_changes(levels), worst])
	_ok("scene_sleep_single_dip", direction_changes(levels) <= 2 and float(levels.max()) - float(levels.min()) > 0.1)
	# 실제 프레임 간격이 들쭉날쭉해서(헤드리스) 한 프레임 값이 아닌 절대 상한의 2배로 봐요
	_ok("scene_sleep_rate_bounded", worst <= RATE_ABS_PER_S * 2.0, "%.3f/s" % worst)
	cp.queue_free()
	await process_frame
	Session.scenario_id = ""


func _initialize() -> void:
	var args := U.parse_args()
	SaveStore.dir = String(args["out"]).path_join("c15_saves")
	run_case("skip30", "skip", 30, 0)
	run_case("sleep", "sleep", 0, 0)
	run_case("skip30_interrupted", "skip", 30, 100)
	season_end_case()
	await scene_case()
	U.finish(self, fails)
