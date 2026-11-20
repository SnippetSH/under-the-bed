# 세이브 묶음 파일과 저장 칸 (m6-overview 7장). 코어 바이트는 해석하지 않고 그대로 넣어요.
#
# 파일 모양 (정수는 모두 little-endian u32):
#   0..3   매직 "UTBS"
#   4..7   묶음 형식 버전 (BUNDLE_VERSION)
#   8..23  구역 길이 4개: 요약(JSON), 게임 구역(JSON), 엔진 상태(JSON), 코어 바이트
#   24..55 머리글 뒤 전체(구역 4개 이어 붙인 것)의 SHA-256 (깨짐 검사)
#   56..   구역 4개를 그 순서로 이어 붙임
# 요약: 목록에 보여 줄 표시용 값 (시뮬레이션에 안 들어가요). 게임 구역: SimRunner가 되살릴 값 (자동 물 주기 설정 등).
# 엔진 상태: SimWorld.get_engine_state() (일정표 위치·명령 기록). 코어: SimWorld.save_bytes().
#
# 규칙 (결정 D6): 다른 버전·깨진 세이브는 불러오기를 거부하고 안내해요. 그 파일은 지우지도 덮어쓰지도 않아요
# (같은 칸에 새로 저장할 때는 옛 파일을 "<칸>_old<번호>.utsave"로 비켜 두고 저장해요).
# 쓸 때는 임시 파일에 쓴 뒤 이름을 바꿔요 (쓰다 꺼져도 이전 세이브가 남게).
extends RefCounted

const Cfg = preload("res://scripts/cfg.gd")

const MAGIC := "UTBS"
const BUNDLE_VERSION := 1
const EXT := ".utsave"
const HEADER_SIZE := 56
const QUICK := "quick"
const AUTO := "auto"

# 저장 폴더. 테스트가 다른 폴더로 바꿔 써요 (절대 경로도 돼요)
static var dir := "user://saves"


# ---------- 칸 ----------
static func manual_ids() -> Array:
	var ids := []
	for i in range(int(Cfg.get_v("saves", "manual_slots"))):
		ids.append("slot_%d" % (i + 1))
	return ids


static func canonical_ids() -> Array:
	return manual_ids() + [QUICK, AUTO]


static func slot_label(id: String) -> String:
	if id == QUICK:
		return "빠른 저장"
	if id == AUTO:
		return "자동 저장"
	if id.begins_with("slot_") and id.substr(5).is_valid_int():
		return "저장 칸 %s" % id.substr(5)
	return id


static func path_of(id: String) -> String:
	return dir.path_join(id + EXT)


static func exists(id: String) -> bool:
	return FileAccess.file_exists(path_of(id))


# ---------- 묶음 만들기·풀기 ----------
static func pack(summary: Dictionary, game: Dictionary, engine: Dictionary, core: PackedByteArray) -> PackedByteArray:
	var secs: Array[PackedByteArray] = [
		JSON.stringify(summary).to_utf8_buffer(), JSON.stringify(game).to_utf8_buffer(),
		JSON.stringify(engine).to_utf8_buffer(), core]
	var payload := PackedByteArray()
	for s in secs:
		payload.append_array(s)
	var ctx := HashingContext.new()
	ctx.start(HashingContext.HASH_SHA256)
	ctx.update(payload)
	var digest := ctx.finish()
	var out := PackedByteArray()
	out.resize(HEADER_SIZE)
	for i in range(4):
		out[i] = MAGIC.unicode_at(i)
	out.encode_u32(4, BUNDLE_VERSION)
	for i in range(4):
		out.encode_u32(8 + 4 * i, secs[i].size())
	for i in range(32):
		out[24 + i] = digest[i]
	out.append_array(payload)
	return out


static func _json_dict(bytes: PackedByteArray) -> Variant:
	var v: Variant = JSON.parse_string(bytes.get_string_from_utf8())
	return v if typeof(v) == TYPE_DICTIONARY else null


static func _bad(kind: String, message: String, extra: Dictionary = {}) -> Dictionary:
	var d := {"ok": false, "kind": kind, "message": message, "summary": {}}
	d.merge(extra)
	return d


# 결과 {ok, kind, message, summary, game, engine, core}. kind: "ok" / "bundle_version" / "corrupt"
static func unpack(bytes: PackedByteArray) -> Dictionary:
	if bytes.size() < HEADER_SIZE:
		return _bad("corrupt", "파일이 너무 짧아요 (%d바이트)" % bytes.size())
	for i in range(4):
		if bytes[i] != MAGIC.unicode_at(i):
			return _bad("corrupt", "세이브 파일이 아니에요 (머리글이 달라요)")
	var version := bytes.decode_u32(4)
	if version != BUNDLE_VERSION:
		return _bad("bundle_version", "저장 형식 v%d, 지금 v%d" % [version, BUNDLE_VERSION])
	var lens: Array[int] = []
	var total := 0
	for i in range(4):
		lens.append(bytes.decode_u32(8 + 4 * i))
		total += lens[i]
	if HEADER_SIZE + total != bytes.size():
		return _bad("corrupt", "파일 길이가 맞지 않아요 (끊겼거나 덧붙었어요)")
	var ctx := HashingContext.new()
	ctx.start(HashingContext.HASH_SHA256)
	ctx.update(bytes.slice(HEADER_SIZE))
	if ctx.finish() != bytes.slice(24, 56):
		return _bad("corrupt", "내용이 바뀌었어요 (검사값이 달라요)")
	var offset := HEADER_SIZE
	var secs: Array = []
	for i in range(4):
		secs.append(bytes.slice(offset, offset + lens[i]))
		offset += lens[i]
	var summary: Variant = _json_dict(secs[0])
	var game: Variant = _json_dict(secs[1])
	var engine: Variant = _json_dict(secs[2])
	if summary == null or game == null or engine == null:
		return _bad("corrupt", "구역을 읽을 수 없어요")
	return {"ok": true, "kind": "ok", "message": "", "summary": summary, "game": game, "engine": engine, "core": secs[3]}


# ---------- 판정 ----------
# 바이트 하나가 지금 불러올 수 있는지. 결과 {status: "ok"/"other_version"/"corrupt", can_load, notice, summary, core_version}
# 전체 역직렬화는 하지 않아요 (peek_save_version은 머리글만 읽어요).
static func inspect(bytes: PackedByteArray) -> Dictionary:
	var u := unpack(bytes)
	if not u["ok"]:
		if u["kind"] == "bundle_version":
			return _verdict("other_version", _foreign_notice(u["message"]), {})
		return _verdict("corrupt", _corrupt_notice(u["message"]), {})
	var peek: Dictionary = SimWorld.peek_save_version(u["core"])
	if not peek["ok"]:
		return _verdict("corrupt", _corrupt_notice("코어 머리글: %s" % peek["message"]), u["summary"])
	var now := SimWorld.core_save_version()
	if int(peek["version"]) != now:
		var d := _verdict("other_version", _foreign_notice("세이브 v%d, 지금 v%d" % [int(peek["version"]), now]), u["summary"])
		d["core_version"] = int(peek["version"])
		return d
	var ok := _verdict("ok", "", u["summary"])
	ok["can_load"] = true
	ok["core_version"] = now
	return ok


static func _verdict(status: String, notice: String, summary: Dictionary) -> Dictionary:
	return {"status": status, "can_load": false, "notice": notice, "summary": summary, "core_version": -1}


static func _foreign_notice(detail: String) -> String:
	return "이 세이브는 다른 버전의 게임에서 만들었어요 (%s). 불러올 수 없어요. 파일은 그대로 두었어요." % detail


static func _corrupt_notice(detail: String) -> String:
	return "이 세이브 파일을 읽을 수 없어요: %s. 불러올 수 없어요. 파일은 그대로 두었어요." % detail


# ---------- 목록 ----------
# 정해진 칸(수동·빠른·자동)은 비어 있어도 항목이 있고, 그 밖의 *.utsave(비켜 둔 옛 파일 등)는 뒤에 이름순으로 붙어요.
# 항목: {id, label, kind("manual"/"quick"/"auto"/"other"), exists, status("empty"/"ok"/"other_version"/"corrupt"),
#        can_load, notice, summary, modified_unix}
static func list_saves() -> Array:
	var out: Array = []
	var seen := {}
	for id in canonical_ids():
		seen[id + EXT] = true
		out.append(_entry(id))
	var extra: Array = []
	var dir_abs := ProjectSettings.globalize_path(dir)
	# 저장 폴더가 아직 없으면(처음 실행) 목록은 빈 칸뿐이에요 (없는 폴더를 열면 엔진이 오류를 찍어요)
	for f in (DirAccess.get_files_at(dir_abs) if DirAccess.dir_exists_absolute(dir_abs) else PackedStringArray()):
		if f.ends_with(EXT) and not seen.has(f):
			extra.append(f.trim_suffix(EXT))
	extra.sort()
	for id in extra:
		out.append(_entry(id))
	return out


static func _entry(id: String) -> Dictionary:
	var kind := "other"
	if id in manual_ids():
		kind = "manual"
	elif id == QUICK or id == AUTO:
		kind = id
	var e := {"id": id, "label": slot_label(id), "kind": kind, "exists": exists(id), "status": "empty",
		"can_load": false, "notice": "", "summary": {}, "modified_unix": 0}
	if not e["exists"]:
		return e
	e["modified_unix"] = FileAccess.get_modified_time(path_of(id))
	var v := inspect(FileAccess.get_file_as_bytes(path_of(id)))
	e["status"] = v["status"]
	e["can_load"] = v["can_load"]
	e["notice"] = v["notice"]
	e["summary"] = v["summary"]
	return e


# 불러올 수 있는 칸 중 가장 최근에 저장한 것의 id (없으면 "")
static func latest_loadable() -> String:
	var best := ""
	var best_t := -1.0
	for e in list_saves():
		if e["can_load"]:
			var t := float(e["summary"].get("saved_at_unix", 0.0))
			if t > best_t:
				best_t = t
				best = e["id"]
	return best


# 목록에 보여 줄 한 줄 (요약이 있을 때)
static func describe(e: Dictionary) -> String:
	if not e["exists"]:
		return "(비어 있음)"
	var s: Dictionary = e["summary"]
	if s.is_empty():
		return "(읽을 수 없음)"
	var saved := ""
	if s.has("saved_at_unix"):
		var local := int(float(s["saved_at_unix"])) + int(Time.get_time_zone_from_system()["bias"]) * 60
		saved = Time.get_datetime_string_from_unix_time(local, true)
	return "%d월 %d일 %02d시 · %d일 경과 · %s · 저장 %s" % [
		int(s.get("month", 0)), int(s.get("day", 0)), int(s.get("hour", 0)), int(s.get("elapsed_days", 0)),
		str(s.get("scenario_label", "?")), saved]


# ---------- 저장 ----------
static func build_summary(runner: Node, kind: String) -> Dictionary:
	var w: Object = runner.world
	var t: Dictionary = w.get_time()
	return {
		"save_kind": kind,
		"saved_at_unix": Time.get_unix_time_from_system(),  # 표시용, 시뮬레이션에 안 들어가요
		"tick": w.get_tick(),
		"elapsed_days": t["elapsed_days"], "day_of_year": t["day_of_year"], "month": t["month"], "day": t["day"], "hour": t["hour"],
		"scenario_id": runner.scenario_id, "scenario_label": runner.scenario_label,
		"mode": runner.mode, "season_ticks": runner.season_ticks, "schedule_enabled": w.is_schedule_enabled(),
		"game_version": str(Cfg.get_v("game", "version")),
		"core_save_version": SimWorld.core_save_version(), "bundle_version": BUNDLE_VERSION,
	}


static func build_bundle(runner: Node, kind: String) -> PackedByteArray:
	var w: Object = runner.world
	return pack(build_summary(runner, kind), runner.game_state(), w.get_engine_state(), w.save_bytes())


# 지금 판을 칸 id에 저장해요. 결과 {ok, message, path, preserved(비켜 둔 옛 파일 id, 없으면 "")}
static func save(runner: Node, id: String) -> Dictionary:
	var kind := id if id == QUICK or id == AUTO else "manual"
	return write_bytes(build_bundle(runner, kind), id)


static func _write_fail(message: String, preserved: String = "") -> Dictionary:
	return {"ok": false, "message": message, "path": "", "preserved": preserved}


static func write_bytes(bytes: PackedByteArray, id: String) -> Dictionary:
	var dir_abs := ProjectSettings.globalize_path(dir)
	var mk := DirAccess.make_dir_recursive_absolute(dir_abs)
	if mk != OK:
		return _write_fail("저장 폴더를 만들 수 없어요 (%d)" % mk)
	var final_path := path_of(id)
	var tmp := final_path + ".tmp"
	var f := FileAccess.open(tmp, FileAccess.WRITE)
	if f == null:
		return _write_fail("파일을 열 수 없어요: %s" % tmp)
	f.store_buffer(bytes)
	f.flush()
	f.close()
	var d := DirAccess.open(dir_abs)
	if d == null:
		return _write_fail("저장 폴더를 열 수 없어요")
	if FileAccess.get_file_as_bytes(tmp).size() != bytes.size():
		d.remove(tmp.get_file())
		return _write_fail("임시 파일을 다 쓰지 못했어요 (디스크가 가득 찼을 수 있어요)")
	var preserved := ""
	var backup := ""
	if FileAccess.file_exists(final_path):
		if inspect(FileAccess.get_file_as_bytes(final_path))["status"] != "ok":
			# 다른 버전이거나 깨진 옛 파일: 덮어쓰지 않고 비켜 둬요 (D6)
			var n := 1
			while FileAccess.file_exists(dir.path_join("%s_old%d%s" % [id, n, EXT])):
				n += 1
			preserved = "%s_old%d" % [id, n]
			if d.rename(final_path.get_file(), preserved + EXT) != OK:
				d.remove(tmp.get_file())
				return _write_fail("옛 파일을 비켜 둘 수 없어요")
		else:
			backup = final_path.get_file() + ".bak"
			if d.file_exists(backup):
				d.remove(backup)
			if d.rename(final_path.get_file(), backup) != OK:
				d.remove(tmp.get_file())
				return _write_fail("이전 저장을 옮길 수 없어요")
	var e := d.rename(tmp.get_file(), final_path.get_file())
	if e != OK:
		if backup != "":
			d.rename(backup, final_path.get_file())  # 이전 저장 되돌리기
		d.remove(tmp.get_file())
		return _write_fail("이름 바꾸기에 실패했어요 (%d)" % e, preserved)
	if backup != "":
		d.remove(backup)
	return {"ok": true, "message": "", "path": ProjectSettings.globalize_path(final_path), "preserved": preserved}


# ---------- 불러오기 ----------
# 새 SimWorld를 만들어 runner에 끼워 넣어요. 실패하면 runner의 이전 월드와 파일은 그대로예요.
# 결과 {ok, status("ok"/"missing"/"other_version"/"corrupt"), message, tick}
static func load_into(runner: Node, id: String) -> Dictionary:
	var path := path_of(id)
	if not FileAccess.file_exists(path):
		return {"ok": false, "status": "missing", "message": "저장 파일이 없어요", "tick": -1}
	var bytes := FileAccess.get_file_as_bytes(path)
	var v := inspect(bytes)
	if v["status"] != "ok":
		return {"ok": false, "status": v["status"], "message": v["notice"], "tick": -1}
	var u := unpack(bytes)
	var w: Object = SimWorld.new()
	var r: Dictionary = w.load_bytes(u["core"], u["engine"])
	if not r["ok"]:
		var status := "other_version" if r["code"] == "UnsupportedVersion" else "corrupt"
		var notice := _foreign_notice(str(r["message"])) if status == "other_version" else _corrupt_notice(str(r["message"]))
		return {"ok": false, "status": status, "message": notice, "tick": -1}
	runner.adopt_loaded(w, u["game"])
	return {"ok": true, "status": "ok", "message": "", "tick": w.get_tick()}
