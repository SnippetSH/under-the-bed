# G2 (m6-overview 10.1 E10, 10.2 H6·H9를 게임 스크립트 수준으로): 진짜 SimRunner 코드로 검사해요.
#  A) 자동 물 주기(직접 플레이, 8시 4 mm)를 SimRunner가 넣으면 기록은 하루에 irrigate 하나씩이고, 기록을 [[schedule]]로
#     내보내 sim-cli로 다시 돌리면 모든 틱 해시가 같음 (E10)
#  B) 설정을 바꾸면 바로 따름 (F3): 오늘 정한 시각 전이면 오늘부터, 이미 지났으면 다음 날, 하루 한 번, 양 변경, 끄기
#  C) 거부된 명령은 신호를 내고 기록에 안 남고 게임은 계속됨 (H3)
#  D) 작기 종료: 설정한 틱에서 정확히 멈춤(프레임당 틱 상한에 걸려도), 일시정지, 한 번만 알림, 계속 진행 가능, 종료 때 자동 저장
#  E) 자동 처방 관전(season_standard) 263일 전체를 tick_frame으로 돌려 종료 화면의 최종 해시가 sim-cli의
#     final_state_hash와 같음, 수확 개수도 같음 (H6). CLI 값은 이 검사가 직접 sim-cli를 돌려 읽어요
# 만든 노드는 끝에 free()해요.
#   godot --headless --path game --script res://tests/game_runner.gd -- out=<폴더> cli=<sim-cli> [days=30]
extends SceneTree

const U = preload("res://tests/util.gd")
const SimRunner = preload("res://scripts/sim_runner.gd")
const SaveStore = preload("res://scripts/save_store.gd")
const Cfg = preload("res://scripts/cfg.gd")

var fails := PackedStringArray()
var nodes: Array = []
var args := {}


func check(cond: bool, what: String) -> void:
	if not cond:
		fails.push_back(what)


func make_runner(id: String, auto_on: Variant = null, season_ticks: int = -1, autosave: bool = false) -> Node:
	var r: Node = SimRunner.new()
	r.load_config()
	var entry := Cfg.scenario(id)
	if auto_on != null:
		entry["auto_irrigation"] = auto_on
	if season_ticks >= 0:
		entry["season_ticks"] = season_ticks
	var res: Dictionary = r.start_new(entry)
	if not res["ok"]:
		fails.push_back("start_new %s: %s" % [id, res])
	r.autosave_enabled = autosave
	nodes.append(r)
	return r


# 기록에서 irrigate 항목의 틱 목록
func irrigate_ticks(r: Node) -> Array:
	var out: Array = []
	for e in r.world.get_command_log():
		if e["command"]["action"] == "irrigate":
			out.append(int(e["tick"]))
	return out


func _init() -> void:
	args = U.parse_args()
	SaveStore.dir = String(args["out"]).path_join("g2_saves")
	part_a()
	part_b()
	part_c()
	part_d()
	part_e()
	for n in nodes:
		n.free()
	U.finish(self, fails)


# ---------- A ----------
func part_a() -> void:
	var days := int(args.get("days", "30"))
	var ticks := days * 24
	var r := make_runner("direct_play")
	check(r.auto_irrigation["enabled"], "A: direct play should start with auto irrigation on")
	var hashes := PackedStringArray([r.world.state_hash()])
	for t in range(ticks):
		r.step(1)
		hashes.push_back(r.world.state_hash())
	var log: Array = r.world.get_command_log()
	var shape_ok := log.size() == days
	for i in range(log.size()):
		var e: Dictionary = log[i]
		if int(e["tick"]) != 8 + 24 * i or e["command"]["action"] != "irrigate" or e["source"] != "player" \
				or float(e["command"]["amount_mm"]) != 4.0:
			shape_ok = false
	print("A days=%d log=%d one_irrigate_per_day_at_8=%s final=%s" % [days, log.size(), shape_ok, hashes[-1]])
	check(shape_ok, "A: the log should be one irrigate per day at hour 8")
	var scenario: String = Cfg.scenario("direct_play")["path"]
	var replay := U.write_replay(args["out"], "g2_auto_irrigation.toml", scenario, log)
	if replay == "":
		fails.push_back("A: cannot write the replay scenario")
	elif args["cli"] == "":
		print("A engine_to_cli SKIPPED (no cli=)")
	else:
		var cli_hashes := U.run_cli(args["cli"], args["params"], replay, ticks,
				ProjectSettings.globalize_path(String(args["out"]).path_join("g2_auto_irrigation_cli.csv")))
		var diff := U.compare(hashes, cli_hashes)
		print("A engine_to_cli rows=%d %s" % [cli_hashes.size(), "MATCH" if diff == "" else diff])
		check(diff == "", "A: engine to CLI: %s" % diff)


# ---------- B ----------
func part_b() -> void:
	# 8시를 이미 넣은 날 시각을 14시로 바꿔도 그날 두 번 넣지 않아요 (하루 한 번)
	var r := make_runner("direct_play")
	r.step(10)  # 0일째 10시
	check(irrigate_ticks(r) == [8], "B1: day 0 should be watered at tick 8, got %s" % [irrigate_ticks(r)])
	r.set_auto_irrigation(true, 14, 4.0)
	r.step(8)  # 18시
	check(irrigate_ticks(r) == [8], "B1: no second watering on day 0 after moving the hour to 14: %s" % [irrigate_ticks(r)])
	r.step(24)  # 다음 날 18시: 14시에 한 번
	check(irrigate_ticks(r) == [8, 24 + 14], "B1: day 1 should be watered at hour 14: %s" % [irrigate_ticks(r)])

	# 오늘 정한 시각이 아직 안 지났으면 오늘부터
	var r2 := make_runner("direct_play", false)
	r2.step(6)
	check(irrigate_ticks(r2).is_empty(), "B2: auto irrigation off should add nothing")
	r2.set_auto_irrigation(true, 8, 4.0)
	r2.step(4)
	check(irrigate_ticks(r2) == [8], "B2: turned on at hour 6 with hour 8: watered today at 8: %s" % [irrigate_ticks(r2)])

	# 이미 지났으면 다음 날부터
	var r3 := make_runner("direct_play", false)
	r3.step(10)
	r3.set_auto_irrigation(true, 8, 4.0)
	r3.step(24 + 6 - 10)  # 다음 날 6시 (tick 30)
	check(irrigate_ticks(r3).is_empty(), "B3: hour 8 already passed today: nothing before tomorrow: %s" % [irrigate_ticks(r3)])
	r3.step(4)
	check(irrigate_ticks(r3) == [24 + 8], "B3: watered tomorrow at 8: %s" % [irrigate_ticks(r3)])

	# 양을 바꾸면 그날 정한 시각부터 새 양, 끄면 그날부터 안 넣음
	var r4 := make_runner("direct_play")
	r4.step(5)
	r4.set_auto_irrigation(true, 8, 7.5)
	r4.step(4)
	var amounts: Array = []
	for e in r4.world.get_command_log():
		amounts.append(float(e["command"]["amount_mm"]))
	check(amounts == [7.5], "B4: new amount should apply today: %s" % [amounts])
	r4.step(24 - 9 + 7)  # 다음 날 7시
	r4.set_auto_irrigation(false, 8, 7.5)
	r4.step(24)
	check(irrigate_ticks(r4) == [8], "B4: turned off at hour 7: no watering tomorrow: %s" % [irrigate_ticks(r4)])


# ---------- C ----------
func part_c() -> void:
	var rejected: Array = []
	var r := make_runner("season_standard")
	r.command_rejected.connect(func(m: String, c: Dictionary): rejected.append([m, c]))
	r.step(48)
	var log_before: int = r.world.get_command_log().size()
	var res: Dictionary = r.submit({"action": "plant", "seedling_grade": "B", "plants_per_m2": 8.0})  # 작물이 있어서 거부
	check(not res["ok"] and rejected.size() == 1 and str(rejected[0][0]) != "", "C: plant on a live crop should be rejected with a signal: %s" % res)
	check(r.world.get_command_log().size() == log_before and r.world.get_pending_command_count() == 0, "C: a rejected command must not be queued or logged")
	var can: Dictionary = r.world.can_enqueue({"action": "plant", "seedling_grade": "B", "plants_per_m2": 8.0})
	check(not can["ok"], "C: can_enqueue should agree with the rejection")
	var tick_before: int = r.world.get_tick()
	r.step(10)
	check(r.world.get_tick() == tick_before + 10, "C: the game should continue after a rejection")
	# 휴작기 밖 겹수 변경 (이 시나리오는 253일에 시작해서 91..243일 밖이에요)
	var cover: Dictionary = r.submit({"action": "set_cover_layers", "layers": 3})
	check(not cover["ok"] and rejected.size() == 2, "C: set_cover_layers outside the off-season should be rejected: %s" % cover)


# ---------- D ----------
func part_d() -> void:
	var ended: Array = []
	var r := make_runner("season_standard", null, 100, true)
	r.season_ended.connect(func(s: Dictionary): ended.append(s))
	r.set_speed(4)  # 초당 240틱
	var frames := 0
	while ended.is_empty() and frames < 20:
		r.tick_frame(0.2)  # 프레임당 48틱 (상한). 100틱은 3프레임째에서 끝
		frames += 1
	check(ended.size() == 1, "D: season_ended should fire exactly once, got %d" % ended.size())
	if ended.size() == 1:
		var s: Dictionary = ended[0]
		check(int(s["tick"]) == 100 and r.world.get_tick() == 100, "D: must stop at exactly tick 100, got %d / %d" % [s["tick"], r.world.get_tick()])
		check(r.speed_index == 0 and frames == 3, "D: should be paused after 3 frames, speed %d frames %d" % [r.speed_index, frames])
		var plain: SimWorld = U.new_world(args["params"], Cfg.scenario("season_standard")["path"], true)
		plain.advance(100)
		check(String(s["state_hash"]) == plain.state_hash(), "D: end-screen hash %s should equal a plain 100-tick run %s" % [s["state_hash"], plain.state_hash()])
		print("D season end at tick %d hash %s frames %d" % [s["tick"], s["state_hash"], frames])
	r.tick_frame(0.2)
	check(r.world.get_tick() == 100, "D: a paused runner must not advance")
	var auto_entry := SaveStore.list_saves().filter(func(e): return e["id"] == "auto")
	check(not auto_entry.is_empty() and auto_entry[0]["can_load"] and int(auto_entry[0]["summary"]["tick"]) == 100,
			"D: the season end should write the autosave slot with tick 100")
	# 계속 진행: 다시 돌릴 수 있고 종료 알림은 다시 안 나와요
	r.set_speed(4)
	r.tick_frame(0.2)
	check(r.world.get_tick() == 148 and ended.size() == 1, "D: continue after the end: tick %d, notices %d" % [r.world.get_tick(), ended.size()])
	# 종료 알림을 받은 판을 저장·불러오면 다시 알리지 않아요
	check(SaveStore.save(r, "slot_1")["ok"], "D: save after the season end")
	var loaded := make_runner("season_standard", null, 100)
	loaded.season_ended.connect(func(s: Dictionary): ended.append(s))
	check(SaveStore.load_into(loaded, "slot_1")["ok"] and loaded.season_over_notified, "D: season_over_notified should be restored")
	loaded.set_speed(4)
	loaded.tick_frame(0.2)
	check(ended.size() == 1, "D: a loaded game past the end must not notify again")


# ---------- E ----------
func part_e() -> void:
	var entry := Cfg.scenario("season_standard")
	var ticks: int = entry["season_ticks"]
	check(ticks == 6312, "E: the standard season should be 263 days = 6312 ticks, got %d" % ticks)
	var autosaves: Array = []
	var ended: Array = []
	var r := make_runner("season_standard", null, -1, true)
	r.autosaved.connect(func(res: Dictionary): autosaves.append(res))
	r.season_ended.connect(func(s: Dictionary): ended.append(s))
	r.set_speed(4)
	var frames := 0
	while ended.is_empty() and frames < 1000:
		r.tick_frame(0.2)
		frames += 1
	check(ended.size() == 1, "E: the season end should fire once, got %d" % ended.size())
	if ended.is_empty():
		return
	var s: Dictionary = ended[0]
	check(int(s["tick"]) == ticks, "E: should end at tick %d, got %d" % [ticks, s["tick"]])
	check(autosaves.size() == ticks / 168 + 1, "E: expected %d autosaves (every 7 days + season end), got %d" % [ticks / 168 + 1, autosaves.size()])
	var all_ok := true
	for a in autosaves:
		all_ok = all_ok and a["ok"]
	check(all_ok, "E: every autosave should succeed")
	check(float(s["harvest"]["harvested_fruits"]) > 0.0, "E: the end screen should show harvest > 0 (H1/H6)")
	print("E frames=%d end tick=%d hash=%s fruits=%d fresh_g=%.3f" % [frames, s["tick"], s["state_hash"], s["harvest"]["harvested_fruits"], s["harvest"]["harvested_fresh_g"]])
	if args["cli"] == "":
		print("E engine_to_cli SKIPPED (no cli=)")
		return
	var out := []
	var code := OS.execute(args["cli"], ["--params", ProjectSettings.globalize_path(args["params"]),
			"--scenario", ProjectSettings.globalize_path(entry["path"]), "--ticks", str(ticks), "--no-hash"], out, true)
	check(code == 0, "E: sim-cli exit %d" % code)
	var cli_hash := ""
	var cli_count := -1
	for line in "".join(out).split("\n"):
		if line.begins_with("final_state_hash="):
			cli_hash = line.trim_prefix("final_state_hash=").strip_edges()
		elif line.begins_with("harvest_total "):
			for part in line.split(" "):
				if part.begins_with("count="):
					cli_count = int(part.trim_prefix("count="))
	print("E sim-cli final_state_hash=%s harvest_count=%d" % [cli_hash, cli_count])
	check(cli_hash != "" and String(s["state_hash"]) == cli_hash, "E: end-screen hash %s vs sim-cli %s" % [s["state_hash"], cli_hash])
	check(int(s["harvest"]["harvested_fruits"]) == cli_count, "E: harvested fruits %d vs sim-cli %d" % [s["harvest"]["harvested_fruits"], cli_count])
