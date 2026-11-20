# 세이브 묶음 파일 (프로토타입: JSON 한 파일, 코어 바이트는 base64). 위치 user://saves/.
# 정식 구현에서는 머리글 + 구역 길이 형식으로 바꿀 수 있어요(m6-overview 7.2).
extends RefCounted

const DIR := "user://saves"
const BUNDLE_FORMAT := 1
const QUICK := "quick.json"


static func build_bundle(runner: Node) -> Dictionary:
	var w: Object = runner.world
	var t: Dictionary = w.get_time()
	return {
		"bundle_format": BUNDLE_FORMAT,
		"summary": {
			"saved_at_unix": Time.get_unix_time_from_system(),  # 표시용, 시뮬레이션에 안 들어가요
			"tick": w.get_tick(),
			"day_of_year": t["day_of_year"],
			"scenario": runner.scenario_path,
			"mode": runner.mode,
		},
		"engine": w.get_engine_state(),
		"game": runner.game_state(),
		"core_base64": Marshalls.raw_to_base64(w.save_bytes()),
	}


# 임시 파일에 쓴 뒤 이름 바꾸기
static func write_bundle(bundle: Dictionary, file_name: String) -> Dictionary:
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(DIR))
	var path := DIR.path_join(file_name)
	var tmp := path + ".tmp"
	var f := FileAccess.open(tmp, FileAccess.WRITE)
	if f == null:
		return {"ok": false, "message": "파일을 열 수 없어요: %s" % tmp}
	f.store_string(JSON.stringify(bundle))
	f.close()
	var d := DirAccess.open(DIR)
	if d == null:
		return {"ok": false, "message": "저장 폴더를 열 수 없어요"}
	if d.file_exists(file_name):
		d.remove(file_name)
	var e := d.rename(tmp.get_file(), file_name)
	if e != OK:
		return {"ok": false, "message": "이름 바꾸기 실패 (%d)" % e}
	return {"ok": true, "path": ProjectSettings.globalize_path(path)}


static func quick_save(runner: Node) -> Dictionary:
	return write_bundle(build_bundle(runner), QUICK)


static func exists(file_name: String = QUICK) -> bool:
	return FileAccess.file_exists(DIR.path_join(file_name))


# 새 SimWorld를 만들어 runner에 끼워 넣어요. 실패하면 이전 월드 그대로(거부·안내, 파일은 건드리지 않아요).
static func load_into(runner: Node, file_name: String = QUICK) -> Dictionary:
	var path := DIR.path_join(file_name)
	if not FileAccess.file_exists(path):
		return {"ok": false, "message": "저장 파일이 없어요"}
	var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(path))
	if typeof(parsed) != TYPE_DICTIONARY or int(parsed.get("bundle_format", -1)) != BUNDLE_FORMAT:
		return {"ok": false, "message": "세이브 묶음 형식을 알 수 없어요 (다른 버전의 게임에서 만들었을 수 있어요)"}
	var w: Object = ClassDB.instantiate("SimWorld")
	var r: Dictionary = w.load_bytes(Marshalls.base64_to_raw(str(parsed["core_base64"])), parsed["engine"])
	if not r["ok"]:
		return {"ok": false, "message": "불러오지 못했어요 (%s): %s" % [r["code"], r["message"]]}
	runner.world = w
	runner.restore_game_state(parsed["game"])
	return {"ok": true, "tick": w.get_tick()}
