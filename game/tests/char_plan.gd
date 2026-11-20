# C4·C5·C6가 같이 쓰는 가짜 캐릭터 판 (도구, 누른 채 걷기, 장치, 자러 가기, 며칠 맡기기, 맡기기 중 끊기,
# 속도 단추 ×1~×4, 1시간 건너뛰기, 도구 설정, 자동 물 주기 조절기). 가짜 입력은 틱 기준이에요(Driver 참고).
# 판: game_config [scenario_direct_play] (빈 밭, 자동 물 주기 켜짐, 결정 D5).
extends RefCounted

const Driver = preload("res://tests/char_driver.gd")

const SCENARIO_ID := "direct_play"

# 도구 index: 0 물뿌리개, 1 비료, 2 퇴비, 3 석회, 4 괭이, 5 멀칭, 6 소독제, 7 유익균, 8 가위, 9 뽑기, 10 천적
const PLAN := [
	["hold", 0, 1, 2],          # 퇴비: 왼쪽 벽 통로에서 0행 전체
	["use", 21, 3],             # 석회 1칸
	["hold", 1, -1, 4],         # 괭이: 가운데 통로에서 1행 전체
	["wait", 1],
	["device", "bench_plant", {"action": "plant", "seedling_grade": "B", "plants_per_m2": 8.0}],
	["device", "heater", {"action": "set_heater", "on": true, "setpoint_degC": 8.0}],
	["wait", 2],
	["setting", 1, "potassium_sulfate", 7.5],   # 비료 설정 바꾸기
	["setting", 0, "", 6.0],                    # 물뿌리개 6 mm
	["speed", 2],              # 속도 ×3 (저장에 들어가야 해요)
	["save"],
	["hold", 1, 1, 0],          # 물뿌리개: 2행 전체
	["sleep"],
	["use", 47, 1], ["use", 48, 1], ["use", 13, 1],
	["skiphour"],               # 대기 중인 행동 3개가 바로 다음 틱에 적용
	["speed", 3],
	["use", 14, 0],
	["skiphour_paused"],        # 일시정지 중에는 건너뛰기 안 됨
	["skiphour"],
	["device", "window_panel", {"action": "set_window", "layer": 0, "side": "left", "open_fraction": 0.5}],
	["wait", 3],
	["skip", 3, 0],
	["setting", 5, "black_film", 0.0],
	["hold", 2, -1, 5],         # 멀칭: 3행 전체
	["device", "fan", {"action": "set_fan", "on": true}],
	["skip", 10, 20],           # 열흘 맡기기, 20프레임 뒤 아무 키 → 다음 기상 시각에 멈춤
	["use", 9, 10], ["use", 26, 7],
	["auto", true, 9, 3.0],     # 자동 물 주기 조절기: 9시 3 mm
	["speed", 0],
	["wait", 2],
	["sleep"],
	["skip", 5, 0],
	["use", 30, 8], ["use", 79, 9],
	["wait", 1],
]


# PLAN[from..]을 돌려요. on_save가 있으면 "save"에서 on_save(s, op_index)를 불러요.
# 돌려주는 값 {ok, at(실패한 op), stops [[틱, 시각, 돌린 틱]], frames, skip_checks [bool]}
static func run(s: Object, from: int, on_save: Callable = Callable()) -> Dictionary:
	var d: Object = Driver.new(s)
	var stops: Array = []
	var skip_checks: Array = []
	for i in range(from, PLAN.size()):
		var op: Array = PLAN[i]
		match op[0]:
			"use":
				if not d.use_tool(op[1], op[2]):
					return {"ok": false, "at": i}
			"hold":
				d.hold_walk(op[1], op[2], op[3], -0.6, s.layout.length_m + 0.6)
			"wait":
				d.wait_ticks(op[1])
			"device":
				if not d.open_device(op[1]):
					return {"ok": false, "at": i}
				var r: Dictionary = s.device_command(op[2])
				if not r["ok"]:
					printerr("device command rejected at op %d: %s" % [i, r])
					return {"ok": false, "at": i}
			"setting":
				var st: Dictionary = s.tool_settings[op[1]]
				if st.has("kind") and op[2] != "":
					st["kind"] = op[2]
				if st.has("amount"):
					st["amount"] = op[3]
			"auto":
				if not d.open_device("auto_irrigation"):
					return {"ok": false, "at": i}
				s.set_auto_irrigation(op[1], op[2], op[3])
			"sleep":
				if not d.open_device("door"):
					return {"ok": false, "at": i}
				s.go_to_sleep()
				d.fast_forward()
				stops.append([s.world().get_tick(), s.hour_now(), 0])
			"skip":
				if not d.open_device("door"):
					return {"ok": false, "at": i}
				var t0: int = s.world().get_tick()
				s.skip_days(op[1])
				d.fast_forward(op[2])
				stops.append([s.world().get_tick(), s.hour_now(), s.world().get_tick() - t0])
			"speed":
				s.set_speed(op[1])
			"skiphour":
				var t1: int = s.world().get_tick()
				var p1: int = s.held_count() + s.world().get_pending_command_count()
				var ok1: bool = s.skip_hour()
				skip_checks.append(ok1 and s.world().get_tick() == t1 + 1 and s.held_count() + s.world().get_pending_command_count() == 0 and p1 > 0)
			"skiphour_paused":
				s.paused = true
				var t2: int = s.world().get_tick()
				skip_checks.append(not s.skip_hour() and s.world().get_tick() == t2)
				s.paused = false
			"save":
				if on_save.is_valid():
					on_save.call(s, i)
	return {"ok": true, "at": -1, "stops": stops, "frames": d.frames, "skip_checks": skip_checks}


static func save_op_index() -> int:
	for i in range(PLAN.size()):
		if PLAN[i][0] == "save":
			return i
	return -1
