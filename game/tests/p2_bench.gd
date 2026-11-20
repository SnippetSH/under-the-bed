# P2 측정: season_standard(일정표 켬)를 warm틱까지 돌린 뒤(과실이 많이 달린 때), 한 틱씩 진행하며
#   advance(1), state_hash(), 화면 한 장 분량 조회(스냅샷 복사 포함), 같은 조회 두 번째(캐시), 진짜 값 열 하나
# 의 평균 시간(ms)을 재요. 배속 "열흘 = 1초"(240틱/초)의 프레임 예산 판단용이에요.
#   godot --headless --path game --script res://tests/p2_bench.gd -- [warm=3000] [samples=240]
extends SceneTree

const U = preload("res://tests/util.gd")
const RIPENING_MIN_INDEX := 0.8  # 게임 쪽 표현 문턱 (게임용 임의값, 측정용)


func screen(w: Object) -> int:
	# 화면이 프레임마다 읽을 것 전부 (게임 화면 + HUD). 반환값은 최적화로 빠지지 않게 쓰는 값
	var v: Dictionary = w.get_tile_visuals(RIPENING_MIN_INDEX)
	var h: Dictionary = w.get_house_controls()
	var we: Dictionary = w.get_weather()
	var t: Dictionary = w.get_time()
	var p: int = w.get_pending_command_count()
	var tot: Dictionary = w.get_harvest_totals()
	return v["fruit_count"].size() + h.size() + we.size() + t.size() + p + tot.size()


func _init() -> void:
	var args := U.parse_args()
	var warm := int(args.get("warm", "3000"))
	var samples := int(args.get("samples", "240"))
	var w: Object = U.new_world("res://data/scenarios/m4/season_standard.toml", true)
	var t0 := Time.get_ticks_usec()
	w.advance(warm)
	var warm_ms := (Time.get_ticks_usec() - t0) / 1000.0
	var adv := 0
	var hsh := 0
	var scr := 0
	var scr2 := 0
	var col := 0
	var cell := 0
	var sink := 0
	for i in range(samples):
		var a := Time.get_ticks_usec()
		w.advance(1)
		var b := Time.get_ticks_usec()
		sink += w.state_hash().length()
		var c := Time.get_ticks_usec()
		sink += screen(w)
		var d := Time.get_ticks_usec()
		sink += screen(w)
		var e := Time.get_ticks_usec()
		sink += w.get_true_column("soil", "water_mm").size()
		var f := Time.get_ticks_usec()
		sink += w.get_true_crop(41).size() + w.get_true_cell(41).size()
		var g := Time.get_ticks_usec()
		adv += b - a
		hsh += c - b
		scr += d - c
		scr2 += e - d
		col += f - e
		cell += g - f
	var v: Dictionary = w.get_tile_visuals(RIPENING_MIN_INDEX)
	var flowers := 0
	var fruits := 0
	for i in range(v["fruit_count"].size()):
		flowers += v["open_flower_count"][i]
		fruits += v["fruit_count"][i]
	var n := float(samples) * 1000.0
	print("build=%s warm_ticks=%d warm_ms=%.1f (%.3f ms/tick) flowers=%d fruits=%d sink=%d" % [
		args.get("label", "?"), warm, warm_ms, warm_ms / warm, flowers, fruits, sink])
	print("per_call_ms advance1=%.4f state_hash=%.4f screen_first=%.4f screen_cached=%.4f true_column=%.4f true_crop_cell=%.4f" % [
		adv / n, hsh / n, scr / n, scr2 / n, col / n, cell / n])
	# 열흘 = 1초 = 240틱/초. 60 fps면 프레임당 4틱 + 화면 1회
	var frame := 4.0 * adv / n + scr / n
	print("frame_at_240_ticks_per_s_60fps_ms=%.3f (budget 16.7) with_hash_per_tick=%.3f" % [frame, frame + 4.0 * hsh / n])
	quit(0)
