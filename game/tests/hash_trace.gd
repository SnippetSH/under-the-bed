# E2 엔진 쪽 (m6-overview 10.1): 시나리오를 일정표대로 헤드리스로 돌리며 틱마다 상태 해시를 "tick,state_hash" CSV로 써요.
# 비교는 tools/compare_engine_cli.py가 해요(같은 트리의 sim-cli --out의 state_hash 열과 모든 틱).
#   godot --headless --path game --script res://tests/hash_trace.gd -- scenario=<경로> ticks=<N> csv=<출력 CSV> [params=...]
# 경로는 res://, user://, 절대 경로 모두 돼요. 일정표 끼우기는 래퍼(advance)가 sim-cli와 같은 순서로 해요.
# state_hash는 한 번에 수 ms라(17.1 U6) 이 검사 전용이에요. 게임은 매 틱 부르지 않아요.
extends SceneTree

const U = preload("res://tests/util.gd")


func _init() -> void:
	var args := U.parse_args()
	var fails := PackedStringArray()
	if not args.has("scenario") or not args.has("ticks") or not args.has("csv"):
		fails.push_back("usage: -- scenario=<path> ticks=<N> csv=<out.csv> [params=<path>]")
		U.finish(self, fails)
		return
	var ticks := int(args["ticks"])
	var w: SimWorld = U.new_world(args["params"], args["scenario"], true)
	if w == null:
		fails.push_back("load failed")
		U.finish(self, fails)
		return
	var out := FileAccess.open(args["csv"], FileAccess.WRITE)
	if out == null:
		fails.push_back("cannot open %s" % args["csv"])
		U.finish(self, fails)
		return
	out.store_string("tick,state_hash\n0,%s\n" % w.state_hash())
	var started := Time.get_ticks_usec()
	for t in range(ticks):
		var a: Dictionary = w.advance(1)
		if not a["ok"]:
			fails.push_back("advance failed at tick %d: %s" % [t, a["message"]])
			break
		out.store_string("%d,%s\n" % [w.get_tick(), w.state_hash()])
	out.close()
	print("ticks=%d total_ms=%.1f final_state_hash=%s" % [w.get_tick(), (Time.get_ticks_usec() - started) / 1000.0, w.state_hash()])
	U.finish(self, fails)
