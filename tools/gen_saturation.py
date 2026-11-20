"""포화 수증기 농도 곡선(humidity.saturation_vapor_curve)을 Tetens 식(물 위)으로 만들어요.

docs/spec/house-humidity.md 3장, ADR-0004: 코어는 표만 읽고 exp를 쓰지 않아요.
  e(hPa) = 6.1078 * exp(17.27 T / (T + 237.3))
  절대 습도(g/m3) = 216.7 * e / (T + 273.15)

참고: 명세 표의 -5, 30, 35, 40, 50, 70 °C 값은 이 식의 3자리 반올림보다 0.01~0.3 크다
(예: 50 °C 82.7 대 83.0). data/params.toml은 명세 표의 값을 그대로 써요(차이 0.4% 이내).

사용: python tools/gen_saturation.py
"""
import math

TEMPS_DEGC = [-10, -5, 0, 5, 10, 15, 20, 25, 30, 35, 40, 50, 70]
A, B = 17.27, 237.3


def saturation_g_per_m3(t_degC: float) -> float:
    e_hPa = 6.1078 * math.exp(A * t_degC / (t_degC + B))
    return 216.7 * e_hPa / (t_degC + 273.15)


def fmt(v: float) -> str:
    # 3자리 유효숫자, 불필요한 0 제거
    s = f"{float(f'{v:.3g}'):g}"
    return s


def main() -> None:
    pts = [f"[{t},{fmt(saturation_g_per_m3(t))}]" for t in TEMPS_DEGC]
    print("saturation_vapor_curve = [" + ", ".join(pts) + "]")


if __name__ == "__main__":
    main()
