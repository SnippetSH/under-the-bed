# P1: 시나리오를 헤드리스로 돌려 틱마다 상태 해시를 "tick,hash"로 써요 (E2 축소판).
#   godot --headless --path game --script res://tests/p1_hashes.gd -- <params> <scenario> <ticks> <out.csv>
# 경로는 res://, user://, 절대 경로 모두 돼요. 일정표 끼우기는 래퍼(advance)가 sim-cli와 같은 순서로 해요.
extends SceneTree


func _init() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() != 4:
		printerr("usage: -- <params> <scenario> <ticks> <out.csv>")
		quit(2)
		return
	var ticks := int(args[2])
	print("engine=%s" % Engine.get_version_info()["string"])
	var w: Object = ClassDB.instantiate("SimWorld")
	if w == null:
		printerr("SimWorld class not registered")
		quit(1)
		return
	var r: Dictionary = w.load_files(args[0], args[1])
	if not r["ok"]:
		printerr("load failed: %s %s" % [r["code"], r["message"]])
		quit(1)
		return
	print("loaded weather=%s schedule=%d" % [r["weather_path"], r["schedule_size"]])
	var out := FileAccess.open(args[3], FileAccess.WRITE)
	if out == null:
		printerr("cannot open %s" % args[3])
		quit(1)
		return
	print("fp_state process=godot at=before_step %s" % ClassDB.class_call_static("SimWorld", "fp_state"))
	out.store_string("tick,state_hash\n0,%s\n" % w.state_hash())
	var started := Time.get_ticks_usec()
	var step_usec := 0
	for t in range(ticks):
		var s0 := Time.get_ticks_usec()
		var a: Dictionary = w.advance(1)
		step_usec += Time.get_ticks_usec() - s0
		if not a["ok"]:
			printerr("advance failed at tick %d: %s" % [t, a["message"]])
			quit(1)
			return
		out.store_string("%d,%s\n" % [w.get_tick(), w.state_hash()])
	out.close()
	var total_ms := (Time.get_ticks_usec() - started) / 1000.0
	print("fp_state process=godot at=after_run %s" % ClassDB.class_call_static("SimWorld", "fp_state"))
	print("ticks=%d total_ms=%.1f advance_ms=%.1f final_state_hash=%s" % [ticks, total_ms, step_usec / 1000.0, w.state_hash()])
	quit(0)
