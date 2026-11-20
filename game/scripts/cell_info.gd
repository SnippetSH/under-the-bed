# "칸 정보" 글 (M6b P13). 보이는 것 get_tile_visuals()만 써요(사람이 눈으로 셀 수 있는 것).
# 단위: 코어의 작물 하나는 칸(1 m²)의 포기 전체를 대표해요(m6-overview 3장). 그래서 세는 값은 "이 칸 포기 전체"의 개수예요.
#  - open_flower_count: 아직 열매가 맺히지 않은 꽃. 화방이 생길 때 꽃이 한꺼번에 목록에 생기고 개화 판정 전까지 여기 들어가서,
#    아직 안 핀 꽃봉오리도 포함돼요(코어 fruit.cpp: set = false). 그래서 "꽃·꽃봉오리"라고 적어요.
#  - fruit_count: 맺힌 열매(익어 가는 것 포함), ripening_fruit_count: 그중 익어 가는 것(게임 쪽 문턱 이상).
# 포기당 값 = 칸 개수 ÷ 재식 밀도(포기/m², 칸 = 1 m²). 밀도는 게임이 아는 값(정식 명령 기록 또는 시나리오 시작값)이에요.
extends RefCounted


# cell < 0 → "" (칩 숨김)
static func text(v: Dictionary, cell: int, plants_per_m2: float, preview_plant := false) -> String:
	if cell < 0 or v.is_empty() or cell >= (v["crop_present"] as PackedByteArray).size():
		return ""
	if preview_plant and (v["crop_present"][cell] == 0 or v["crop_alive"][cell] == 0):
		return "방금 심은 모종 (다음 시간에 자리 잡아요)"
	if v["crop_present"][cell] == 0:
		return "빈 칸"
	if v["crop_alive"][cell] == 0:
		return "죽은 포기"
	var fl: int = v["open_flower_count"][cell]
	var fr: int = v["fruit_count"][cell]
	var rp: int = v["ripening_fruit_count"][cell]
	var who := "이 칸 포기 전체"
	if plants_per_m2 > 0.0:
		who = "이 칸 포기 %s개 모두" % _num(plants_per_m2)
	var line1 := "%s: 꽃·꽃봉오리 %d개 · 열매 %d개 (그중 익어 가는 것 %d개)" % [who, fl, fr, rp]
	if plants_per_m2 <= 0.0:
		return line1
	return line1 + "\n포기당 꽃·꽃봉오리 약 %s개 · 열매 약 %s개" % [_num(fl / plants_per_m2), _num(fr / plants_per_m2)]


static func _num(x: float) -> String:
	var r := snappedf(x, 0.1)
	return str(int(r)) if is_equal_approx(r, roundf(r)) else "%.1f" % r
