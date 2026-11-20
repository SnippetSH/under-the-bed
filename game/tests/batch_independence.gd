# E7 (m6-overview 10.1, 3.3): 배속과 무관.
# 같은 명령(시나리오 일정표 + 플레이어 명령)을 advance 묶음 크기만 바꿔 (1틱씩 / 한 번에 7틱 / 명령 사이를 한 번에) 돌려요.
# 세 실행의 최종 해시, 명령 기록, 이벤트 수가 같아야 해요. 플레이어 명령은 정한 틱 직전에 넣어요(SimRunner와 같은 방식).
#   godot --headless --path game --script res://tests/batch_independence.gd [-- ticks=1500]
extends SceneTree

const U = preload("res://tests/util.gd")
const SCENARIO := "res://data/scenarios/m4/season_standard.toml"

var player := {
	30: [{"action": "set_fan", "on": true}],
	31: [{"action": "irrigate", "area": [0, 0, 2, 5], "amount_mm": 2.0}],
	200: [{"action": "add_fertilizer", "fertilizer": "urea", "g_per_m2": 1.0},
		  {"action": "set_window", "layer": 0, "side": "right", "open_fraction": 0.3}],
	777: [{"action": "set_heater", "on": true, "setpoint_degC": 7.0}],
	1201: [{"action": "thin_flowers", "area": [0, 0, 4, 10], "per_plant": 3.0}],
}


# batch = 한 번의 advance에 넘기는 최대 틱 수. 플레이어 명령이 있는 틱에서는 끊어요.
func run(params: String, ticks: int, batch: int) -> Dictionary:
	var w: SimWorld = U.new_world(params, SCENARIO, true)
	var keys: Array = player.keys()
	keys.sort()
	var events := 0
	var t := 0
	var calls := 0
	while t < ticks:
		for c in player.get(t, []):
			var r: Dictionary = w.enqueue(c)
			if not r["ok"]:
				return {"error": "enqueue failed at %d: %s" % [t, r]}
		var stop := mini(ticks, t + batch)
		for k in keys:
			if k > t and k < stop:
				stop = k
				break
		var a: Dictionary = w.advance(stop - t)
		if not a["ok"]:
			return {"error": "advance failed: %s" % a}
		calls += 1
		events += w.take_events().size()
		t = stop
	return {"hash": w.state_hash(), "log": SimWorld.format_schedule_toml(w.get_command_log()), "events": events,
			"calls": calls, "tick": w.get_tick()}


func _init() -> void:
	var args := U.parse_args()
	var ticks := int(args.get("ticks", "1500"))
	var fails := PackedStringArray()
	var results := []
	for batch in [1, 7, ticks]:
		var r := run(args["params"], ticks, batch)
		if r.has("error"):
			fails.push_back(r["error"])
			U.finish(self, fails)
			return
		print("batch=%d advance_calls=%d tick=%d events=%d hash=%s" % [batch, r["calls"], r["tick"], r["events"], r["hash"]])
		results.append(r)
	for i in range(1, results.size()):
		for key in ["hash", "log", "events", "tick"]:
			if results[i][key] != results[0][key]:
				fails.push_back("batch run %d differs in %s" % [i, key])
	U.finish(self, fails)
