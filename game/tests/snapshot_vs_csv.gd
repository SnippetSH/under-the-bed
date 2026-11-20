# E9 (m6-overview 10.1): 스냅샷 변환.
# season_standard를 일정표대로 돌리며 정한 틱에서 get_true_column·get_true_crop 값의 하우스 평균을 내고,
# 같은 트리의 sim-cli CSV 같은 틱 행의 값과 비교해요. CSV는 "왕복되는 최단 표기"라서 CSV에 찍힌 자릿수까지 같아야 해요
# (글자 → 실수 변환 차이만 허용: 상대 1e-12).
#   godot --headless --path game --script res://tests/snapshot_vs_csv.gd -- out=<폴더> cli=<sim-cli>
extends SceneTree

const U = preload("res://tests/util.gd")
const SCENARIO := "res://data/scenarios/m4/season_standard.toml"
const STOPS := [0, 500, 2000, 4000]
const REL_TOL := 1e-12
# [CSV 열, 종류, 필드]. 흙은 모든 칸 평균, 작물은 작물이 있는 칸 평균 (sim-cli build_row와 같은 규칙)
const FIELDS := [
	["water_mm", "soil", "water_mm"],
	["ph", "soil", "ph"],
	["soil_temp_degC", "soil", "soil_temp_degC"],
	["no3_N_g_per_m2", "soil", "no3_N_g_per_m2"],
	["ec_dS_per_m", "soil", "ec_dS_per_m"],
	["decomposer_index", "soil", "microbe_index.decomposer"],
	["leaf_g_per_m2", "crop", "leaf_g_per_m2"],
	["root_g_per_m2", "crop", "root_g_per_m2"],
	["stress_hours_water", "crop", "stress_hours.water"],
]


func mean_of(values: PackedFloat64Array, mask: PackedByteArray) -> float:
	var sum := 0.0
	var n := 0
	for i in range(values.size()):
		if mask.is_empty() or mask[i] == 1:
			sum += values[i]
			n += 1
	return sum / float(n) if n > 0 else NAN


func read_csv_rows(path: String, rows: Array) -> Dictionary:
	# 결과: {틱: {열 이름: 글자}}
	var out := {}
	var f := FileAccess.open(path, FileAccess.READ)
	if f == null:
		return out
	var header := f.get_csv_line()
	var t := 0
	while not f.eof_reached():
		var line := f.get_csv_line()
		if rows.has(t) and line.size() == header.size():
			var d := {}
			for i in range(header.size()):
				d[header[i]] = line[i]
			out[t] = d
		t += 1
	f.close()
	return out


func close_enough(a: float, b: float) -> bool:
	return absf(a - b) <= REL_TOL * maxf(1.0, absf(b))


func _init() -> void:
	var args := U.parse_args()
	var fails := PackedStringArray()
	if args["cli"] == "":
		fails.push_back("cli= is required")
		U.finish(self, fails)
		return
	var last: int = STOPS[-1]
	var csv: String = ProjectSettings.globalize_path(args["out"].path_join("snapshot_vs_csv_cli.csv"))
	var cli_hashes := U.run_cli(args["cli"], args["params"], ProjectSettings.globalize_path(SCENARIO), last, csv)
	if cli_hashes.size() != last + 1:
		fails.push_back("sim-cli run failed")
		U.finish(self, fails)
		return
	var rows := read_csv_rows(csv, STOPS)
	var w: SimWorld = U.new_world(args["params"], SCENARIO, true)
	var compared := 0
	for stop in STOPS:
		w.advance(stop - w.get_tick())
		var row: Dictionary = rows.get(stop, {})
		if row.is_empty():
			fails.push_back("CSV has no row for tick %d" % stop)
			continue
		var present: PackedByteArray = w.get_tile_visuals(1.0)["crop_present"]
		var engine_values := {}
		for f in FIELDS:
			var col: PackedFloat64Array = w.get_true_column(f[1], f[2])
			if col.size() != present.size():
				fails.push_back("get_true_column(%s, %s) returned %d values" % [f[1], f[2], col.size()])
				continue
			engine_values[f[0]] = mean_of(col, present if f[1] == "crop" else PackedByteArray())
		# 파생값(nutrient_status)은 get_true_crop에서: 작물이 있는 칸 평균
		var suff := 0.0
		var n := 0
		for i in range(present.size()):
			if present[i] == 1:
				suff += float(w.get_true_crop(i)["nutrient_status"]["sufficiency_N"])
				n += 1
		engine_values["sufficiency_N"] = suff / float(n) if n > 0 else NAN
		for key in engine_values:
			var text: String = row.get(key, "")
			var engine: float = engine_values[key]
			if text == "":
				# 작물 열은 작물이 없으면 CSV가 비워 둬요
				if not is_nan(engine):
					fails.push_back("tick %d %s: CSV blank, engine %s" % [stop, key, engine])
				continue
			if not close_enough(engine, text.to_float()):
				fails.push_back("tick %d %s: engine %.17g vs CSV %s" % [stop, key, engine, text])
			compared += 1
		if w.state_hash() != cli_hashes[stop]:
			fails.push_back("tick %d state hash differs from CLI" % stop)
	print("ticks=%s fields=%d compared_values=%d" % [STOPS, FIELDS.size() + 1, compared])
	U.finish(self, fails)
