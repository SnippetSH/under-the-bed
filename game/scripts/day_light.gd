# 시각 빛 (꾸밈, 진짜 값 없음): 시각 → 빛 값(화면 곱하기 색, 하늘, 해 방향·색·세기, 해/달 아이콘).
# 그리고 자러 가기·며칠 맡기기 동안의 "낮 → 밤 → 낮 한 번" 표현 (광과민 대응, 디렉터 요청 2026-11-20):
#  - 빨리 돌리는 동안 빛은 시뮬레이션 시계를 따르지 않아요. 시작할 때 보이던 빛에서 밤으로 천천히, 밤에 잠깐 머물렀다가
#    기상 시각의 빛으로 천천히, 며칠이 지나든 딱 한 번만 바뀌어요. 진행 정도는 CharSession.rest_progress()(0~1)가 정해요.
#  - 시뮬레이션은 같은 시간 동안 같은 틱을 고르게 나눠 돌려서(char_session.advance_clock) 둘이 함께 끝나요.
#    끝나면 기상 시각의 빛 = 시계의 빛이라 이어서 시계를 따라도 튀지 않아요.
#  - 도중에 일하는 상태로 돌아오면(작기 끝으로 멈춤, 불러오기) 그때 보이던 빛에서 시계의 빛으로 rest_settle_seconds 동안 부드럽게 옮겨요.
# 그리기를 하지 않아서 헤드리스 검사(C15)가 그대로 몰 수 있어요. char_play가 프레임마다 update()를 부르고 그 값으로 그려요.
extends RefCounted

const Cfg = preload("res://scripts/cfg.gd")

# 시각(시 + 몫) → 화면 곱하기 색 (예전 char_play.LIGHT_KEYS 그대로 옮김, 게임용 임의값)
const LIGHT_KEYS := [
	[0.0, Color(0.42, 0.47, 0.72)], [4.5, Color(0.45, 0.49, 0.74)], [6.0, Color(0.98, 0.8, 0.74)],
	[7.5, Color(1, 0.97, 0.92)], [12.0, Color(1, 1, 1)], [16.0, Color(1, 0.97, 0.9)],
	[18.0, Color(1.0, 0.88, 0.76)], [19.5, Color(0.74, 0.68, 0.86)], [21.0, Color(0.44, 0.48, 0.73)], [24.0, Color(0.42, 0.47, 0.72)],
]
const SKY_BASE := Color("#d6e8ee")
# 해 (예전 house_view_3d.set_light 그대로 옮김): 6시에 떠서 13시간, 높이 8~62도, 방위 -70~70도
const SUNRISE_H := 6.0
const DAY_LENGTH_H := 13.0
const SUN_ELEV_DEG := [8.0, 62.0]
const SUN_AZ_DEG := [-70.0, 70.0]
const SUN_WARM := Color(1.0, 0.72, 0.5)
const SUN_NOON := Color(1.0, 0.97, 0.9)
const SUN_COLOR_GAIN := 1.6
const SUN_ENERGY_GAIN := 1.4
const SUN_ENERGY_MIN := 0.06
const ICON_DAY_H := [6.0, 19.0]      # 해 아이콘을 보이는 시각 [부터, 전까지]
const NIGHT_HOUR := 0.0              # "밤"의 빛 = 자정의 빛

var hold_fraction := 0.2             # [char_time] rest_night_hold_fraction
var settle_seconds := 1.5            # [char_time] rest_settle_seconds
var _last := {}
var _start := {}
var _in_rest := false
var _settle_from := {}
var _settle_t := -1.0


func _init() -> void:
	hold_fraction = clampf(float(Cfg.get_v("char_time", "rest_night_hold_fraction")), 0.0, 1.0)
	settle_seconds = float(Cfg.get_v("char_time", "rest_settle_seconds"))


static func tint_at(h: float) -> Color:
	h = fposmod(h, 24.0)
	for i in range(LIGHT_KEYS.size() - 1):
		var a: Array = LIGHT_KEYS[i]
		var b: Array = LIGHT_KEYS[i + 1]
		if h >= a[0] and h <= b[0]:
			return (a[1] as Color).lerp(b[1], smoothstep(a[0], b[0], h))
	return LIGHT_KEYS[0][1]


# 시각 h(시 + 몫)의 빛 값
static func params(h: float) -> Dictionary:
	h = fposmod(h, 24.0)
	var day := clampf(sin((h - SUNRISE_H) / DAY_LENGTH_H * PI), 0.0, 1.0)
	return {
		"tint": tint_at(h),
		"sun_elev_deg": lerpf(SUN_ELEV_DEG[0], SUN_ELEV_DEG[1], day),
		"sun_az_deg": lerpf(SUN_AZ_DEG[0], SUN_AZ_DEG[1], clampf((h - SUNRISE_H) / DAY_LENGTH_H, 0.0, 1.0)),
		"sun_color": SUN_WARM.lerp(SUN_NOON, clampf(day * SUN_COLOR_GAIN, 0.0, 1.0)),
		"sun_energy_factor": clampf(day * SUN_ENERGY_GAIN, SUN_ENERGY_MIN, 1.0),
		"day_icon": h >= ICON_DAY_H[0] and h < ICON_DAY_H[1],
	}


# 두 빛 값 사이 (w 0 → a, 1 → b). 아이콘은 가운데에서 한 번만 바뀌어요
static func blend(a: Dictionary, b: Dictionary, w: float) -> Dictionary:
	return {
		"tint": (a["tint"] as Color).lerp(b["tint"], w),
		"sun_elev_deg": lerpf(a["sun_elev_deg"], b["sun_elev_deg"], w),
		"sun_az_deg": lerpf(a["sun_az_deg"], b["sun_az_deg"], w),
		"sun_color": (a["sun_color"] as Color).lerp(b["sun_color"], w),
		"sun_energy_factor": lerpf(a["sun_energy_factor"], b["sun_energy_factor"], w),
		"day_icon": a["day_icon"] if w < 0.5 else b["day_icon"],
	}


# 표현 진행 p(0~1)에서의 빛: 시작 → 밤 (앞 몫), 밤에 머묾 (hold), 밤 → 기상 (뒤 몫). 각 구간은 부드럽게(smoothstep)
static func cycle(start: Dictionary, night: Dictionary, wake: Dictionary, p: float, hold: float) -> Dictionary:
	var edge := (1.0 - hold) * 0.5
	if p <= edge:
		return blend(start, night, smoothstep(0.0, edge, p) if edge > 0.0 else 1.0)
	if p < 1.0 - edge:
		return night
	return blend(night, wake, smoothstep(1.0 - edge, 1.0, p) if edge > 0.0 else 1.0)


# 밝기 한 값 (검사·개발자용): 화면 곱하기 색의 밝기. 하늘·안개·주변광도 이 색에 비례해요
static func level(p: Dictionary) -> float:
	return (p["tint"] as Color).get_luminance()


# 한 프레임: s = CharSession, dt = 현실 초. 이번 프레임에 그릴 빛 값을 돌려줘요
func update(s: Object, dt: float) -> Dictionary:
	var clock := params(float(s.hour_now()) + s.hour_fraction())
	var p: Dictionary
	if s.mode != "work":
		if not _in_rest:
			_in_rest = true
			_settle_t = -1.0
			_start = _last if not _last.is_empty() else clock
		p = cycle(_start, params(NIGHT_HOUR), params(float(s.wake_hour)), s.rest_progress(), hold_fraction)
	else:
		if _in_rest:
			_in_rest = false
			_settle_from = _last
			_settle_t = 0.0
		if _settle_t >= 0.0:
			_settle_t += dt
			p = blend(_settle_from, clock, smoothstep(0.0, settle_seconds, _settle_t) if settle_seconds > 0.0 else 1.0)
			if _settle_t >= settle_seconds:
				_settle_t = -1.0
		else:
			p = clock
	_last = p
	return p


func last() -> Dictionary:
	return _last


func last_level() -> float:
	return level(_last) if not _last.is_empty() else 0.0
