# 헤드리스 검사용 "가짜 손" (M6b C1~C14). CharSession을 키 입력처럼 몰아요.
#  - 걷기는 character_frame(이동 방향, 행동 키, dt)만 불러요: 시간이 흐르지 않아요.
#  - 시간은 wait_ticks / sleep / skip이 advance_clock(dt)로만 흘려요. 그래서 입력이 "틱 기준"이에요(C5).
extends RefCounted

const CharSession = preload("res://scripts/char_session.gd")
const Cfg = preload("res://scripts/cfg.gd")

const FRAME_DT := 1.0 / 60.0
const CLOCK_DT := 1.0 / 30.0
const FRONT_Z := -1.0

var s: Object            # CharSession
var opened: Array = []   # device_opened로 열린 장치 id
var frames := 0
var frame_dt := FRAME_DT   # 바꾸면 낮은 프레임(큰 dt)을 흉내 내요
var run := false          # true면 Shift(달리기)를 누른 채 걸어요


func _init(session: Object) -> void:
	s = session
	s.device_opened.connect(_on_opened)   # 메서드 연결 (람다는 이 객체를 붙잡아 세션과 서로 붙잡게 돼요)


func _on_opened(d: Dictionary) -> void:
	opened.append(d["id"])


# 시작 화면과 같은 판 항목(game_config [scenario_<id>])으로 캐릭터 판을 시작해요. auto_on이 null이 아니면 자동 물 주기를 바꿔요.
# 실패하면 null. 다 쓴 뒤에는 dispose()를 불러요(SimRunner 노드를 풀어요).
static func new_session(scenario_id: String, auto_on: Variant = null) -> Object:
	var entry := Cfg.scenario(scenario_id)
	if entry.is_empty():
		printerr("no scenario entry %s" % scenario_id)
		return null
	if auto_on != null:
		entry["auto_irrigation"] = auto_on
	var s: Object = CharSession.new()
	var r: Dictionary = s.start(entry)
	if not r["ok"]:
		printerr("start failed %s" % r)
		s.dispose()
		return null
	return s


func _frame(move: Vector2i, held: bool) -> void:
	s.character_frame(move, held, frame_dt, run)
	frames += 1


# 한 축씩 걸어서 목표에 가까이 (한 걸음보다 가까우면 멈춰요). held = 행동 키를 누른 채
func _walk_axis(axis: int, target: float, held: bool) -> bool:
	var step: float = s.walk_speed * frame_dt * (s.run_factor if run else 1.0)
	for i in range(20000):
		var cur: float = s.pos.x if axis == 0 else s.pos.y
		var d := target - cur
		if absf(d) <= step:
			return true
		var mv := Vector2i(signi(int(signf(d))), 0) if axis == 0 else Vector2i(0, int(signf(d)))
		var before: Vector2 = s.pos
		_frame(mv, held)
		if s.pos == before:
			return false   # 막혔어요
	return false


# 통로를 따라 목표 자리로: 같은 통로가 아니면 출입구 쪽 빈 땅으로 나가서 옆으로 옮겨요
func walk_to(p: Vector2, held := false) -> bool:
	var lay: Object = s.layout
	var same_aisle: bool = lay.aisle_at(s.pos) >= 0 and lay.aisle_at(s.pos) == lay.aisle_at(p)
	if not same_aisle and absf(s.pos.x - p.x) > s.walk_speed * FRAME_DT:
		if not _walk_axis(1, FRONT_Z, held):
			return false
		if not _walk_axis(0, p.x, held):
			return false
	return _walk_axis(1, p.y, held) and _walk_axis(0, p.x, held)


# 바라보기: 그쪽으로 한 걸음 밀어요(막혀 있으면 방향만 바뀌어요)
func face(f: Vector2i) -> void:
	var p: Vector2 = s.pos
	_frame(f, false)
	if s.pos != p:
		_frame(-f, false)  # 되돌아오면 방향이 바뀌니 다시 바라봐요
		_frame(f, false)


func press() -> void:
	_frame(Vector2i.ZERO, true)
	_frame(Vector2i.ZERO, false)


func go_to_cell(cell: int) -> bool:
	var sp: Array = s.layout.stand_point_for_cell(cell)
	if not walk_to(sp[0]):
		return false
	face(sp[1])
	return s.target_cell() == cell


func use_tool(cell: int, tool: int) -> bool:
	if not go_to_cell(cell):
		printerr("cannot reach cell %d (pos %s target %d)" % [cell, s.pos, s.target_cell()])
		return false
	s.select_tool(tool)
	press()
	return true


func open_device(id: String) -> bool:
	var dev: Dictionary = s.layout.device_by_id(id)
	var sp: Array = s.layout.stand_point_for_device(dev)
	if not walk_to(sp[0]):
		printerr("cannot reach device %s" % id)
		return false
	face(sp[1])
	opened.clear()
	press()
	return opened.size() == 1 and opened[0] == id


# 통로 k를 따라 행동 키를 누른 채 걸어요 (side = -1 왼쪽 베드, 1 오른쪽 베드)
func hold_walk(aisle: int, side: int, tool: int, z_from: float, z_to: float) -> void:
	walk_to(Vector2(s.layout.aisle_center_x(aisle), z_from))
	face(Vector2i(side, 0))
	s.select_tool(tool)
	_frame(Vector2i.ZERO, true)
	_walk_axis(1, z_to, true)
	_frame(Vector2i.ZERO, false)


func wait_ticks(n: int) -> int:
	var target: int = s.world().get_tick() + n
	var guard := 0
	while s.world().get_tick() < target and guard < 50000000:
		s.advance_clock(CLOCK_DT)
		guard += 1
	return guard


# 자러 가기·며칠 맡기기: 일하는 상태로 돌아올 때까지 시계를 흘려요. interrupt_after_ticks > 0이면 그만큼 틱이 돈 뒤 아무 키.
# (틱 기준이라 표현 길이·프레임 간격과 무관하게 같은 틱에서 끊겨요. 예전에는 프레임 수였어요: 240틱/초 × 1/30초 × 20 = 160틱)
func fast_forward(interrupt_after_ticks := 0) -> int:
	var f := 0
	var t0: int = s.world().get_tick()
	var interrupted := false
	while s.mode != "work" and f < 1000000:
		s.advance_clock(CLOCK_DT)
		f += 1
		if interrupt_after_ticks > 0 and not interrupted and s.world().get_tick() - t0 >= interrupt_after_ticks:
			interrupted = true
			s.interrupt_skip()
	return f
