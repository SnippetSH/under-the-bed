# G1 (m6-overview 10.1 E3·E5를 게임 스크립트 수준으로): 진짜 SimRunner·SaveStore 코드로 세이브를 검사해요.
#  A) 묶음 왕복: 일정표가 있는 season_standard를 돌려 수동 칸·빠른 저장에 저장 → 새 러너로 불러와 이어 돌린 결과가
#     끊지 않은 실행과 같음 (최종 해시, 명령 기록, 저장 직전 대기 명령). 7일 자동 저장이 정해진 틱에 생기고 불러와짐.
#  B) 자동 물 주기 설정(켜짐·시각·양)과 "오늘 이미 넣었는지"가 묶음의 게임 구역으로 왕복됨 (직접 플레이)
#  C) 다른 버전(코어 v7, 묶음 형식 v2) 세이브: 목록이 불러올 수 없다고 표시하고 안내문에 두 버전이 있음, 불러오기는
#     거부되고 월드·파일이 그대로, 같은 칸에 새로 저장하면 옛 파일이 "<칸>_old1"로 남음 (D6)
#  D) 깨진 파일(빈 파일, 잘린 파일, 매직이 다른 것, 한 비트 바뀐 것, 쓰레기, 본문만 잘린 코어)은 죽지 않고 거부, 파일 그대로
#  E) 임시 파일 정리: 저장 뒤 .tmp·.bak이 남지 않고, 남아 있던 .tmp는 목록에 안 나옴
# 만든 노드는 끝에 free()해요.
#   godot --headless --path game --script res://tests/game_save_store.gd -- out=<폴더>
extends SceneTree

const U = preload("res://tests/util.gd")
const SimRunner = preload("res://scripts/sim_runner.gd")
const SaveStore = preload("res://scripts/save_store.gd")
const Cfg = preload("res://scripts/cfg.gd")

var fails := PackedStringArray()
var nodes: Array = []
var autosave_results: Array = []


func check(cond: bool, what: String) -> void:
	if not cond:
		fails.push_back(what)


func make_runner(id: String) -> Node:
	var r: Node = SimRunner.new()
	r.load_config()
	r.autosaved.connect(func(res: Dictionary): autosave_results.append(res))
	var res: Dictionary = r.start_new(Cfg.scenario(id))
	if not res["ok"]:
		fails.push_back("start_new %s: %s" % [id, res])
	nodes.append(r)
	return r


func clean_dir() -> void:
	var abs_dir := ProjectSettings.globalize_path(SaveStore.dir)
	DirAccess.make_dir_recursive_absolute(abs_dir)
	var d := DirAccess.open(abs_dir)
	for f in DirAccess.get_files_at(abs_dir):
		d.remove(f)


func write_raw(file_name: String, bytes: PackedByteArray) -> void:
	var f := FileAccess.open(SaveStore.dir.path_join(file_name), FileAccess.WRITE)
	f.store_buffer(bytes)
	f.close()


func read_raw(file_name: String) -> PackedByteArray:
	return FileAccess.get_file_as_bytes(SaveStore.dir.path_join(file_name))


func entry_of(id: String) -> Dictionary:
	for e in SaveStore.list_saves():
		if e["id"] == id:
			return e
	return {}


func log_text(r: Node) -> String:
	return SimWorld.format_schedule_toml(r.world.get_command_log())


func _init() -> void:
	var args := U.parse_args()
	SaveStore.dir = String(args["out"]).path_join("g1_saves")
	clean_dir()
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
	var a := make_runner("season_standard")
	check(a.mode == "watch" and a.world.is_schedule_enabled(), "A: watch mode should have the schedule on")
	a.step(500)
	a.submit({"action": "add_fertilizer", "area": [0, 0, 2, 20], "fertilizer": "urea", "g_per_m2": 2.0})
	a.step(340)  # tick 840 = 5 x 168, the 5th autosave
	var hash_840: String = a.world.state_hash()
	var auto_at_840: Dictionary = entry_of("auto")
	check(autosave_results.size() == 5, "A: expected 5 autosaves in 840 ticks, got %d" % autosave_results.size())
	check(auto_at_840["can_load"] and int(auto_at_840["summary"]["tick"]) == 840,
			"A: autosave should hold tick 840, got %s" % [auto_at_840["summary"].get("tick")])
	a.submit({"action": "set_heater", "on": true, "setpoint_degC": 6.0})
	a.step(60)  # tick 900
	a.step(100)  # tick 1000
	a.submit({"action": "irrigate", "area": [0, 0, 1, 1], "amount_mm": 1.0})  # 저장 때 대기 중
	var pending: int = a.world.get_pending_command_count()
	var hash_1000: String = a.world.state_hash()
	var s2 := SaveStore.save(a, "slot_2")
	var sq := SaveStore.save(a, SaveStore.QUICK)
	check(s2["ok"] and sq["ok"], "A: manual and quick save should succeed: %s %s" % [s2, sq])
	check(pending == 1, "A: expected 1 pending command, got %d" % pending)
	var entry := entry_of("slot_2")
	check(entry["can_load"] and entry["kind"] == "manual" and int(entry["summary"]["tick"]) == 1000, "A: slot_2 list entry: %s" % entry)
	check(entry_of("slot_1")["status"] == "empty", "A: slot_1 should be empty")
	check(SaveStore.latest_loadable() != "", "A: latest_loadable should find a save")
	a.step(1000)  # 끊지 않은 실행: tick 2000
	var final_a: String = a.world.state_hash()
	var log_a := log_text(a)

	var b := make_runner("season_standard")
	b.autosave_enabled = false
	var rl := SaveStore.load_into(b, "slot_2")
	check(rl["ok"], "A: load slot_2: %s" % rl)
	check(b.world.get_tick() == 1000 and b.world.state_hash() == hash_1000, "A: loaded tick/hash differ")
	check(b.world.get_pending_command_count() == pending, "A: pending command not restored")
	check(b.speed_index == 0, "A: a loaded game should start paused")
	check(b.mode == "watch" and b.scenario_id == "season_standard" and b.season_ticks == 6312, "A: game section not restored")
	b.step(1000)
	check(b.world.state_hash() == final_a, "A: hash after load + 1000 ticks differs from the uninterrupted run")
	check(log_text(b) == log_a, "A: command log differs after load")
	print("A slot_2 round trip: tick 1000 -> 2000, hash %s %s" % [final_a, "MATCH" if b.world.state_hash() == final_a else "DIFF"])

	# 빠른 저장도 같은 것이어야 하고, 자동 저장(틱 840 이후 2000까지 더 갱신됨)도 불러와져요
	var q := make_runner("season_standard")
	check(SaveStore.load_into(q, SaveStore.QUICK)["ok"] and q.world.get_tick() == 1000 and q.world.state_hash() == hash_1000,
			"A: quick slot differs")
	var c := make_runner("season_standard")
	check(SaveStore.load_into(c, SaveStore.AUTO)["ok"], "A: autosave should load")
	check(c.world.get_tick() == 1848, "A: after 2000 ticks the autosave should hold tick 1848, got %d" % c.world.get_tick())
	check(hash_840 != "", "A: hash_840")

	# 같은 칸에 다시 저장하면 새것으로 바뀌고 .bak이 남지 않아요
	var again := SaveStore.save(a, "slot_2")
	check(again["ok"] and again["preserved"] == "", "A: overwrite of a good slot: %s" % again)
	check(int(entry_of("slot_2")["summary"]["tick"]) == 2000, "A: slot_2 should now hold tick 2000")


# ---------- B ----------
func part_b() -> void:
	var d := make_runner("direct_play")
	d.autosave_enabled = false
	check(d.mode == "direct" and not d.world.is_schedule_enabled(), "B: direct play should have the schedule off")
	check(d.auto_irrigation["enabled"] and d.auto_irrigation["hour"] == 8 and d.auto_irrigation["amount_mm"] == 4.0,
			"B: direct play default auto irrigation")
	d.step(24 * 5 + 10)  # 5일째 10시
	d.set_auto_irrigation(true, 14, 2.5)
	d.step(2)  # 12시: 아직 14시 전, 오늘의 물 주기(8시)는 이미 했어요
	check(SaveStore.save(d, "slot_3")["ok"], "B: save slot_3")
	d.step(24 * 10)
	var final_d: String = d.world.state_hash()
	var log_d := log_text(d)

	var e := make_runner("direct_play")
	e.autosave_enabled = false
	e.set_auto_irrigation(false, 3, 1.0)
	var rl := SaveStore.load_into(e, "slot_3")
	check(rl["ok"], "B: load slot_3: %s" % rl)
	check(e.auto_irrigation["enabled"] and e.auto_irrigation["hour"] == 14 and e.auto_irrigation["amount_mm"] == 2.5,
			"B: auto irrigation settings not restored: %s" % [e.auto_irrigation])
	e.step(24 * 10)
	check(e.world.state_hash() == final_d, "B: hash after load differs")
	check(log_text(e) == log_d, "B: command log differs after load")
	# 이날 8시에 이미 넣었으니 14시로 바꿔도 하루 한 번: 5일째(day 5)에는 irrigate가 하나뿐이에요
	var day5 := 0
	for entry in e.world.get_command_log():
		if entry["command"]["action"] == "irrigate" and int(entry["tick"]) / 24 == 5:
			day5 += 1
	check(day5 == 1, "B: day 5 should have exactly one irrigation, got %d" % day5)
	print("B auto irrigation settings round trip: hash %s %s" % [final_d, "MATCH" if e.world.state_hash() == final_d else "DIFF"])


# ---------- C ----------
func part_c() -> void:
	var good := read_raw("slot_2.utsave")
	var u := SaveStore.unpack(good)
	check(u["ok"], "C: unpack a good bundle")
	var core: PackedByteArray = u["core"].duplicate()
	var now := SimWorld.core_save_version()
	core[4] = now - 1
	var old_core := SaveStore.pack(u["summary"], u["game"], u["engine"], core)
	write_raw("slot_4.utsave", old_core)
	var bundle_v2 := good.duplicate()
	bundle_v2.encode_u32(4, 2)
	write_raw("other_bundle.utsave", bundle_v2)

	var e4 := entry_of("slot_4")
	check(e4["exists"] and e4["status"] == "other_version" and not e4["can_load"], "C: slot_4 should be listed as another version: %s" % e4)
	check(("v%d" % (now - 1)) in e4["notice"] and ("v%d" % now) in e4["notice"], "C: notice should name both versions: %s" % e4["notice"])
	var eb := entry_of("other_bundle")
	check(eb["status"] == "other_version" and not eb["can_load"] and "저장 형식" in eb["notice"], "C: bundle version notice: %s" % eb)
	print("C notice: %s" % e4["notice"])

	var w := make_runner("season_standard")
	w.step(100)
	var hash_before: String = w.world.state_hash()
	var tick_before: int = w.world.get_tick()
	for id in ["slot_4", "other_bundle"]:
		var r := SaveStore.load_into(w, id)
		check(not r["ok"] and r["status"] == "other_version" and r["message"] != "", "C: load %s should be refused: %s" % [id, r])
		check(w.world.get_tick() == tick_before and w.world.state_hash() == hash_before, "C: refused load changed the world")
	check(read_raw("slot_4.utsave") == old_core and read_raw("other_bundle.utsave") == bundle_v2, "C: refused files must stay untouched")

	# 같은 칸에 새로 저장: 옛 파일은 지우지도 덮어쓰지도 않고 비켜 둬요
	var sv := SaveStore.save(w, "slot_4")
	check(sv["ok"] and sv["preserved"] == "slot_4_old1", "C: save over a foreign file should preserve it: %s" % sv)
	check(read_raw("slot_4_old1.utsave") == old_core, "C: the old file should be kept byte for byte")
	check(entry_of("slot_4")["can_load"] and entry_of("slot_4_old1")["status"] == "other_version", "C: list after preserving")
	var sv2 := SaveStore.save(w, "slot_4")
	check(sv2["ok"] and sv2["preserved"] == "", "C: a good slot is simply replaced: %s" % sv2)


# ---------- D ----------
func part_d() -> void:
	var good := read_raw("slot_2.utsave")
	var u := SaveStore.unpack(good)
	var flipped := good.duplicate()
	flipped[flipped.size() - 10] ^= 0x01
	var bad_magic := good.duplicate()
	bad_magic[0] = 0x58
	var half_core: PackedByteArray = SaveStore.pack(u["summary"], u["game"], u["engine"], u["core"].slice(0, u["core"].size() / 2))
	var garbage := PackedByteArray()
	for i in range(100):
		garbage.append((i * 37 + 11) % 256)
	var cases := {
		"bad_empty": PackedByteArray(),
		"bad_truncated": good.slice(0, good.size() / 2),
		"bad_magic": bad_magic,
		"bad_flipped": flipped,
		"bad_garbage": garbage,
		"bad_core_body_cut": half_core,
	}
	var w := make_runner("season_standard")
	w.step(50)
	var hash_before: String = w.world.state_hash()
	for id in cases:
		write_raw(id + ".utsave", cases[id])
	for id in cases:
		var en := entry_of(id)
		var expect_listed_ok: bool = id == "bad_core_body_cut"  # 머리글은 멀쩡해서 목록에서는 불러올 수 있는 것으로 보여요
		check(en["exists"], "D: %s should be listed" % id)
		if not expect_listed_ok:
			check(en["status"] == "corrupt" and not en["can_load"] and en["notice"] != "", "D: %s list entry: %s" % [id, en])
		var r := SaveStore.load_into(w, id)
		check(not r["ok"] and r["status"] == "corrupt" and r["message"] != "", "D: %s should be refused as corrupt: %s" % [id, r])
		check(w.world.state_hash() == hash_before, "D: %s changed the world" % id)
		check(read_raw(id + ".utsave") == cases[id], "D: %s file changed" % id)
		print("D %s -> %s" % [id, r["message"]])
	var missing := SaveStore.load_into(w, "no_such_slot")
	check(not missing["ok"] and missing["status"] == "missing", "D: missing file: %s" % missing)
	# 거부가 이어진 뒤에도 월드는 계속 돌아요
	w.step(10)
	check(w.world.get_tick() == 60, "D: the world should keep running after refused loads")


# ---------- E ----------
func part_e() -> void:
	write_raw("slot_1.utsave.tmp", PackedByteArray([1, 2, 3]))
	check(entry_of("slot_1.utsave.tmp").is_empty() and entry_of("slot_1")["status"] == "empty", "E: a leftover .tmp must not be listed")
	var w := make_runner("direct_play")
	w.step(30)
	check(SaveStore.save(w, "slot_1")["ok"], "E: save slot_1 over a leftover .tmp")
	check(SaveStore.save(w, "slot_1")["ok"], "E: save slot_1 again")
	var stray: Array = []
	for f in DirAccess.get_files_at(ProjectSettings.globalize_path(SaveStore.dir)):
		if f.ends_with(".bak") or (f.ends_with(".tmp") and f != "slot_1.utsave.tmp"):
			stray.append(f)
	check(stray.is_empty(), "E: stray temp/backup files left behind: %s" % [stray])
	check(entry_of("slot_1")["can_load"], "E: slot_1 should load")
	# 저장 폴더가 아직 없는 처음 실행: 목록은 빈 칸뿐이고 엔진 오류가 찍히지 않아요 (검사 실행기가 오류 줄을 잡아요)
	var keep := SaveStore.dir
	SaveStore.dir = keep.path_join("never_created")
	var fresh := SaveStore.list_saves()
	check(fresh.size() == SaveStore.canonical_ids().size() and fresh.all(func(e): return e["status"] == "empty"),
			"E: a missing save folder should list only empty slots")
	check(SaveStore.latest_loadable() == "", "E: nothing to continue in a missing save folder")
	SaveStore.dir = keep
