# 헤드리스 엔진 검사(m6-overview 10.1) 공통 도우미. 검사 스크립트가 preload해서 써요.
# 인자는 "-- key=value ..." 꼴이에요. 공통 키:
#   out=<출력 폴더, 기본 user://m6_tests>  cli=<sim-cli 실행 파일 절대 경로, 없으면 CLI 비교 건너뜀>
#   params=<파라미터 파일, 기본 res://data/params.toml>
# 검사는 끝에 "RESULT PASS" 또는 "RESULT FAIL <이유>"를 찍고 종료 코드 0/1로 끝나요(tools/run_engine_check.py가 읽어요).
extends RefCounted

const PARAMS := "res://data/params.toml"
const WEATHER := "res://data/weather/synthetic_year.csv"


static func parse_args() -> Dictionary:
	var d := {"out": "user://m6_tests", "cli": "", "params": PARAMS}
	for a in OS.get_cmdline_user_args():
		var i := a.find("=")
		if i > 0:
			d[a.substr(0, i)] = a.substr(i + 1)
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(d["out"]))
	return d


static func new_world(params: String, scenario: String, schedule_enabled: bool) -> SimWorld:
	var w := SimWorld.new()
	var r: Dictionary = w.load_files(params, scenario)
	if not r["ok"]:
		printerr("load failed: %s %s" % [r["code"], r["message"]])
		return null
	w.set_schedule_enabled(schedule_enabled)
	return w


static func write_text(path: String, text: String) -> bool:
	var f := FileAccess.open(path, FileAccess.WRITE)
	if f == null:
		printerr("cannot write %s" % path)
		return false
	f.store_string(text)
	f.close()
	return true


# 기록을 일정표로 넣은 재생용 시나리오를 out 폴더에 써요. 기상 CSV는 절대 경로로 바꿔요. 성공하면 절대 경로.
static func write_replay(out_dir: String, name: String, scenario: String, log: Array) -> String:
	var text := FileAccess.get_file_as_string(scenario)
	var r: Dictionary = SimWorld.build_replay_scenario(text, log, ProjectSettings.globalize_path(WEATHER))
	if not r["ok"]:
		printerr("build_replay_scenario failed: %s %s" % [r["code"], r["message"]])
		return ""
	var path := ProjectSettings.globalize_path(out_dir.path_join(name))
	return path if write_text(path, r["text"]) else ""


# sim-cli로 실행하고 CSV의 state_hash 열(틱 0부터)을 돌려줘요. 실패하면 빈 배열.
static func run_cli(cli: String, params: String, scenario_abs: String, ticks: int, csv_abs: String) -> PackedStringArray:
	var out := []
	var args := ["--params", ProjectSettings.globalize_path(params), "--scenario", scenario_abs,
			"--ticks", str(ticks), "--out", csv_abs]
	var code := OS.execute(cli, args, out, true)
	if code != 0:
		printerr("sim-cli exit %d: %s" % [code, "".join(out)])
		return PackedStringArray()
	return read_hash_column(csv_abs)


static func read_hash_column(csv_path: String) -> PackedStringArray:
	var result := PackedStringArray()
	var f := FileAccess.open(csv_path, FileAccess.READ)
	if f == null:
		return result
	var header := f.get_csv_line()
	var col := header.find("state_hash")
	if col < 0:
		return result
	while not f.eof_reached():
		var row := f.get_csv_line()
		if row.size() > col:
			result.push_back(row[col])
	f.close()
	return result


# 같으면 "", 다르면 처음 달라진 위치 (행 = 틱)
static func compare(a: PackedStringArray, b: PackedStringArray) -> String:
	var n := mini(a.size(), b.size())
	for i in range(n):
		if a[i] != b[i]:
			return "first mismatch at tick %d: %s vs %s" % [i, a[i], b[i]]
	if a.size() != b.size():
		return "length %d vs %d" % [a.size(), b.size()]
	return ""


static func hashes_to_csv(hashes: PackedStringArray) -> String:
	var lines := PackedStringArray(["tick,state_hash"])
	for i in range(hashes.size()):
		lines.push_back("%d,%s" % [i, hashes[i]])
	return "\n".join(lines) + "\n"


# 검사 끝: 결과 줄을 찍고 종료 코드를 정해요. fails가 비어 있으면 통과.
static func finish(tree: SceneTree, fails: PackedStringArray) -> void:
	if fails.is_empty():
		print("RESULT PASS")
		tree.quit(0)
	else:
		for f in fails:
			printerr("FAIL: %s" % f)
		print("RESULT FAIL %s" % "; ".join(fails))
		tree.quit(1)
