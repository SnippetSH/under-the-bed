// M5b 시나리오 방향 테스트 (docs/spec/foliar-disease.md 9장 "시나리오" S26~S32·S34, house-humidity.md 8장 "시나리오"
// S26~S28·S26b~S26d). 실제 data/params.toml과 data/scenarios/m5b/*.toml(S34 비교는 m4 season_*)을 읽어
// "기대 방향"(크기 순서)만 검사해요. 명세 7장·6장 목표값(B1~B21)은 주석에 참고로만 적고, 측정값은 WARN으로 출력해요.
// 길이·측정 창은 data/scenarios/m5b/README.md, 측정 정의는 tools/m5b_window_stats.py와 같아요:
//   - 시계열 인덱스 = 처리한 틱 수 (0 = 초기 상태). "창 안"은 인덱스 [창 시작, 끝].
//   - 수확 개수 = 모든 칸 harvest.harvested_fruits 합 (제거한 칸 포함, CSV harvest_total_count),
//     썩은 과실 비율 = 창 안 썩음 ÷ (창 안 수확 + 창 안 썩음).
//   - 젖은 시간 = 살아 있는 포기 중 잎 젖음 ≥ wet_threshold_index인 비율의 틱 합 (포기 평균 젖은 시간 h).
//   - S31 응애 = 하우스 평균 응애 지수 (house.spider_mite_mean_index, CSV house_spider_mite_mean_index).
// 같은 시나리오는 한 테스트 프로세스 안에서 한 번만 돌리고 결과를 캐시해요(M5a 시나리오 테스트와 같은 방식).
// 실행하면서 매 틱 보존·불변 조건(m5b_world_checks.hpp: 원소·물·수증기·지수 0~1·NaN)도 검사해서, 각 테스트가
// 끝에 그 결과를 확인해요 (긴 시즌 시나리오를 보존 검사용으로 다시 돌리지 않으려고).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "m5b_world_checks.hpp"
#include "sim/world.hpp"
#include "simdata/loader.hpp"
#include "test_helpers.hpp"

namespace {

constexpr std::uint64_t kDay = 24;
constexpr std::uint64_t kWeek = 7 * kDay;
constexpr std::size_t kAnth = static_cast<std::size_t>(sim::FoliarDisease::Anthracnose);

// README "길이"
constexpr std::uint64_t kS26Ticks = 3456;      // S26·S27 가을 작기
constexpr std::uint64_t kS26Window = 1968;     // 측정 창 12/1~1/31
constexpr std::uint64_t kHumidityTicks = 1488; // 습도만 보는 변형 (12/1 정식 직후, 62일)
constexpr std::uint64_t kS28Ticks = 2160;
constexpr std::uint64_t kSeasonTicks = 6312;   // S29, S34, m4 season_*
constexpr std::uint64_t kSpringTicks = 5952;   // S30, S32
constexpr std::uint64_t kS30Window = 4872;
constexpr std::uint64_t kS32Window = 4128;
constexpr std::uint64_t kS31Ticks = 4608;
constexpr std::uint64_t kS31Early = 1800;      // 11/25 방사
constexpr std::uint64_t kS31Late = 3600;       // 2/2 방사

using BundleEdit = std::function<void(simdata::ScenarioBundle&)>;

struct RunSpec {
    std::string path;  // data/scenarios/ 아래 (확장자 없음)
    std::uint64_t ticks = 0;
    std::optional<sim::FoliarDisease> remove_on_onset;  // sim-cli --remove-on-onset
    BundleEdit edit;
};

// 한 시나리오 실행의 시계열 (인덱스 = 처리한 틱 수)
struct Run {
    std::vector<int> hour;  // 그 시점 스냅샷의 시각 (CSV hour 열)
    std::vector<double> rh, rh_mean, air_temp;
    std::vector<double> wet_frac;       // 살아 있는 포기 중 젖은 비율
    std::vector<double> leaf_wet_mean;  // 살아 있는 포기 평균 잎 젖음
    std::vector<double> leaf_wet_min;   // 살아 있는 포기 최소 잎 젖음 (없으면 1)
    std::vector<double> max_wet_run;    // 칸 중 가장 긴 연속 젖음 시간
    std::vector<double> overhead_frac;  // 두상 관수 표시 칸 비율
    std::vector<double> botrytis, mildew, anthracnose;  // 작물이 있는 칸 평균 감염
    std::vector<double> house_mite;     // 하우스 평균 응애 지수
    std::vector<double> mite_mean_2w;   // 작물 평균 응애 2주 이동 평균 (광합성 피해 입력)
    std::vector<double> predator;       // 작물 평균 천적
    std::vector<double> harvest_count;  // 모든 칸 정상과 수확 개수 합
    std::vector<double> harvest_fresh;  // 모든 칸 정상과 생체중 합 ÷ 칸 수 (g/m²)
    std::vector<double> rot_count, mildew_fruit_count;
    std::vector<double> condensed_mm, heating_MJ;
    std::array<std::uint64_t, sim::kFoliarDiseaseCount> onsets{};
    std::uint64_t died = 0;
    std::uint64_t died_anthracnose = 0;
    std::size_t cells = 0;
    double wet_threshold = 0.0;
    double overhead_wet = 0.0;
    sim::Ledger ledger;
    // 매 틱 보존·불변 검사
    double max_budget_err = 0.0;
    std::string budget_name;
    std::string invalid;
    std::uint64_t invalid_tick = 0;
};

sim::Params load_repo_params() {
    auto params = simdata::load_params(test::repo_path("data/params.toml"));
    REQUIRE(params.has_value());
    return std::move(*params);
}

void record(Run& run, const sim::Snapshot& s) {
    run.hour.push_back(s.time.hour);
    run.rh.push_back(s.house.rh_index);
    run.rh_mean.push_back(s.house.rh_mean_index);
    run.air_temp.push_back(s.house_air_temp_degC);
    run.house_mite.push_back(s.house.spider_mite_mean_index);
    run.condensed_mm.push_back(s.house.water_condensed_mm);
    run.heating_MJ.push_back(s.house.heating_energy_MJ_per_m2);
    double alive = 0.0, wet = 0.0, w_sum = 0.0, w_min = 1.0, max_run = 0.0;
    double present = 0.0, bot = 0.0, pm = 0.0, anth = 0.0, mite2w = 0.0, pred = 0.0;
    double harvest = 0.0, fresh = 0.0, overhead = 0.0;
    for (std::size_t i = 0; i < s.crops.size(); ++i) {
        const sim::Crop& c = s.crops[i];
        overhead += s.soil[i].overhead_irrigated_this_tick ? 1.0 : 0.0;
        harvest += static_cast<double>(c.harvest.harvested_fruits);
        fresh += c.harvest.harvested_fresh_g;
        if (!c.present) {
            continue;
        }
        present += 1.0;
        bot += c.infection_botrytis_index;
        pm += c.infection_powdery_mildew_index;
        anth += c.infection_anthracnose_index;
        mite2w += c.spider_mite_mean_index;
        pred += c.predator_mite_index;
        max_run = std::max(max_run, c.wet_run_hours);
        if (!c.alive) {
            continue;
        }
        alive += 1.0;
        wet += c.leaf_wetness_index >= run.wet_threshold ? 1.0 : 0.0;
        w_sum += c.leaf_wetness_index;
        w_min = std::min(w_min, c.leaf_wetness_index);
    }
    const double n = static_cast<double>(s.crops.size());
    run.overhead_frac.push_back(overhead / n);
    run.harvest_count.push_back(harvest);
    run.harvest_fresh.push_back(fresh / n);
    run.rot_count.push_back(static_cast<double>(s.ledger.fruit_rot_count));
    run.mildew_fruit_count.push_back(static_cast<double>(s.ledger.harvested_mildew));
    run.max_wet_run.push_back(max_run);
    run.wet_frac.push_back(alive > 0.0 ? wet / alive : 0.0);
    run.leaf_wet_mean.push_back(alive > 0.0 ? w_sum / alive : 0.0);
    run.leaf_wet_min.push_back(w_min);
    run.botrytis.push_back(present > 0.0 ? bot / present : 0.0);
    run.mildew.push_back(present > 0.0 ? pm / present : 0.0);
    run.anthracnose.push_back(present > 0.0 ? anth / present : 0.0);
    run.mite_mean_2w.push_back(present > 0.0 ? mite2w / present : 0.0);
    run.predator.push_back(present > 0.0 ? pred / present : 0.0);
}

Run run_uncached(const RunSpec& spec) {
    auto bundle = simdata::load_scenario_bundle(test::repo_path("data/scenarios/" + spec.path + ".toml"));
    REQUIRE(bundle.has_value());
    if (spec.edit) {
        spec.edit(*bundle);
    }
    std::stable_sort(bundle->schedule.begin(), bundle->schedule.end(),
                     [](const auto& a, const auto& b) { return a.tick < b.tick; });
    auto world = sim::World::create(load_repo_params(), std::move(bundle->scenario));
    REQUIRE(world.has_value());
    const auto& schedule = bundle->schedule;
    const int cols = world->scenario().bed_cols;

    Run run;
    run.wet_threshold = world->params().humidity.wet_threshold_index;
    run.overhead_wet = world->params().humidity.overhead_wet_index;
    sim::Snapshot prev = world->snapshot();
    const sim::Snapshot init_snap = prev;
    const test::m5b::Totals init = test::m5b::totals_of(prev);
    run.cells = prev.soil.size();
    record(run, prev);

    std::size_t next = 0;
    std::vector<sim::Command> removals;  // sim-cli와 같이: 발병 다음 틱 시작 전에, 일정표 명령보다 먼저
    for (std::uint64_t t = 0; t < spec.ticks; ++t) {
        for (sim::Command& c : removals) {
            (void)world->enqueue(std::move(c));  // 이미 빈 칸이면 거부돼요 (sim-cli도 무시)
        }
        removals.clear();
        while (next < schedule.size() && schedule[next].tick == t) {
            REQUIRE(world->enqueue(schedule[next].command).has_value());
            ++next;
        }
        world->step(1);
        for (const sim::Event& e : world->events()) {
            if (const auto* onset = std::get_if<sim::FoliarDiseaseOnsetEvent>(&e.payload)) {
                ++run.onsets[static_cast<std::size_t>(onset->disease)];
                if (spec.remove_on_onset && onset->disease == *spec.remove_on_onset) {
                    const int r = static_cast<int>(onset->tile_index) / cols;
                    const int c = static_cast<int>(onset->tile_index) % cols;
                    removals.emplace_back(sim::RemovePlantCommand{.area = {.row = r, .col = c, .rows = 1, .cols = 1}});
                }
            } else if (const auto* died = std::get_if<sim::PlantDiedEvent>(&e.payload)) {
                ++run.died;
                run.died_anthracnose += died->cause == sim::RootDamageCause::Anthracnose ? 1 : 0;
            }
        }
        sim::Snapshot snap = world->snapshot();
        record(run, snap);
        if (run.invalid.empty()) {
            const auto errs = test::m5b::budget_errors(world->params(), init_snap, init, snap);
            for (std::size_t i = 0; i < errs.size(); ++i) {
                if (errs[i] > run.max_budget_err) {
                    run.max_budget_err = errs[i];
                    run.budget_name = test::m5b::kBudgetNames[i];
                }
            }
            const std::string reason = test::m5b::invalid_reason(prev, snap);
            if (!reason.empty()) {
                run.invalid = reason;
                run.invalid_tick = t + 1;
            }
        }
        prev = std::move(snap);
    }
    run.ledger = prev.ledger;
    return run;
}

const Run& run_scenario(const std::string& key, const RunSpec& spec) {
    static std::map<std::string, Run> cache;
    auto it = cache.find(key);
    if (it == cache.end()) {
        it = cache.emplace(key, run_uncached(spec)).first;
    }
    return it->second;
}

const Run& m5b(const std::string& name, std::uint64_t ticks) {
    return run_scenario(name, RunSpec{.path = "m5b/" + name, .ticks = ticks});
}

// 실행 중 매 틱 보존·불변 검사 결과 (foliar-disease 9장 "보존", house-humidity 7장)
void check_conserved(const std::string& name, const Run& r) {
    INFO(name << ": worst budget " << r.budget_name << " rel err " << r.max_budget_err << ", invalid '" << r.invalid
              << "' at tick " << r.invalid_tick);
    CHECK(r.max_budget_err <= test::m5b::kRelTol);
    CHECK(r.invalid.empty());
}

double mean_range(const std::vector<double>& v, std::size_t from, std::size_t to) {
    to = std::min(to, v.size());
    REQUIRE(from < to);
    double s = 0.0;
    for (std::size_t i = from; i < to; ++i) {
        s += v[i];
    }
    return s / static_cast<double>(to - from);
}
double sum_range(const std::vector<double>& v, std::size_t from, std::size_t to) {
    return mean_range(v, from, to) * static_cast<double>(std::min(to, v.size()) - from);
}
double max_range(const std::vector<double>& v, std::size_t from, std::size_t to) {
    to = std::min(to, v.size());
    REQUIRE(from < to);
    return *std::max_element(v.begin() + static_cast<std::ptrdiff_t>(from), v.begin() + static_cast<std::ptrdiff_t>(to));
}
// 시각 조건을 만족하는 인덱스만의 평균
double mean_hours(const Run& r, const std::vector<double>& v, std::size_t from, std::size_t to,
                  const std::function<bool(int)>& hour_ok) {
    to = std::min(to, v.size());
    double s = 0.0;
    double n = 0.0;
    for (std::size_t i = from; i < to; ++i) {
        if (hour_ok(r.hour[i])) {
            s += v[i];
            n += 1.0;
        }
    }
    REQUIRE(n > 0.0);
    return s / n;
}
bool night_hour(int h) { return h >= 22 || h <= 5; }  // B14 "22~5시"

// 창 [from, to] 안 증가량
double delta(const std::vector<double>& v, std::size_t from, std::size_t to) {
    REQUIRE(to < v.size());
    return v[to] - v[from];
}
// 창 안 썩은 과실 비율 = 썩음 ÷ (수확 + 썩음) (B4 정의)
double rot_ratio(const Run& r, std::size_t from, std::size_t to) {
    const double rot = delta(r.rot_count, from, to);
    const double harv = delta(r.harvest_count, from, to);
    return rot + harv > 0.0 ? rot / (rot + harv) : 0.0;
}
// 밤(16시 ~ 다음 날 10시) 가운데 하우스 평균 잎 젖음 ≥ 문턱인 시간이 min_hours 이상인 밤 수 (B15)
std::size_t long_wet_nights(const Run& r, std::size_t from, std::size_t to, int min_hours) {
    to = std::min(to, r.hour.size());
    std::size_t nights = 0;
    int run_hours = 0;
    bool in_night = false;
    for (std::size_t i = from; i < to; ++i) {
        const int h = r.hour[i];
        const bool night = h >= 16 || h <= 10;
        if (night && !in_night) {
            run_hours = 0;
        }
        if (night) {
            run_hours += r.leaf_wet_mean[i] >= r.wet_threshold ? 1 : 0;
        }
        if (!night && in_night && run_hours >= min_hours) {
            ++nights;
        }
        in_night = night;
    }
    return nights;
}
double reduction(double control, double treated) { return control > 0.0 ? (control - treated) / control : 0.0; }

// 습도 변형의 3겹 6 °C 밀폐 대조 (house-humidity 8장 S26b "s26_layers 방식 6 °C", 옛 겨울 s26_closed):
// s26_heated(옛 s26_closed + 난방만 12 °C)의 난방 설정을 6 °C로 되돌려요. 시나리오 파일은 그대로 두고 테스트에서만 바꿔요.
const Run& humidity_control() {
    return run_scenario("humidity_control_6C",
                        RunSpec{.path = "m5b/s26_heated", .ticks = kHumidityTicks, .edit = [](simdata::ScenarioBundle& b) {
                                    for (auto& sc : b.schedule) {
                                        if (auto* heater = std::get_if<sim::SetHeaterCommand>(&sc.command)) {
                                            heater->setpoint_degC = 6.0;
                                        }
                                    }
                                }});
}

}  // namespace

// ---- S26 닫힘 대 환기 (가을 작기, 측정 창 12/1~1/31) ----

// S26: 닫힌 쪽 습도·젖은 시간·잿빛곰팡이 감염 압력·썩은 과실 비율이 큼
// (완료 기준 "고습 시 병 발생 증가"). 참고 목표값: B4 썩은 과실 10~30% 대 1~5% (6차 10.5% 대 3.5%),
// B2 방향(환기 쪽 낮 습도가 낮음), B11 젖는 밤 5~12 h, B1 밤 평균 85~95%.
TEST_CASE("M5b S26 and S27 closed house without venting or fan is wetter and rots more fruit", "[scenario][m5b][s26][s27]") {
    SECTION("S26 closed house is wetter and rots more fruit than the vented house") {
        const Run& closed = m5b("s26_closed", kS26Ticks);
        const Run& vented = m5b("s26_vented", kS26Ticks);
        const std::size_t w = kS26Window;
        const std::size_t end = kS26Ticks;
        const double rh_c = mean_range(closed.rh, w, end + 1);
        const double rh_v = mean_range(vented.rh, w, end + 1);
        const double day_rh_c = mean_hours(closed, closed.rh, w, end + 1, [](int h) { return h >= 10 && h <= 17; });
        const double day_rh_v = mean_hours(vented, vented.rh, w, end + 1, [](int h) { return h >= 10 && h <= 17; });
        const double wet_c = sum_range(closed.wet_frac, w + 1, end + 1);
        const double wet_v = sum_range(vented.wet_frac, w + 1, end + 1);
        const double run_c = max_range(closed.max_wet_run, w, end + 1);
        const double run_v = max_range(vented.max_wet_run, w, end + 1);
        const double bot_c = mean_range(closed.botrytis, w, end + 1);
        const double bot_v = mean_range(vented.botrytis, w, end + 1);
        const double rot_c = rot_ratio(closed, w, end);
        const double rot_v = rot_ratio(vented, w, end);
        WARN(std::format("S26 closed/vented: rh {:.3f}/{:.3f}, day rh {:.3f}/{:.3f}, wet h {:.0f}/{:.0f}, longest run "
                         "{:.0f}/{:.0f} h, botrytis {:.4f}/{:.4f}, rot {:.1f}%/{:.1f}% (harvest {:.0f}/{:.0f})",
                         rh_c, rh_v, day_rh_c, day_rh_v, wet_c, wet_v, run_c, run_v, bot_c, bot_v, 100 * rot_c, 100 * rot_v,
                         delta(closed.harvest_count, w, end), delta(vented.harvest_count, w, end)));
        CHECK(rh_c > rh_v);
        CHECK(day_rh_c > day_rh_v);  // B2 방향
        CHECK(wet_c > wet_v);
        // 연속 젖음 지표는 "하루 최장 연속 젖음의 창 평균"이에요 (결정 50). 창 전체 최댓값은 두 쪽 모두 17 h라 비교가 안 돼요.
        const auto daily_max_mean = [](const Run& r, std::size_t from, std::size_t to) {
            double s = 0.0;
            double n = 0.0;
            for (std::size_t d = from; d + kDay <= to; d += kDay) {
                s += max_range(r.max_wet_run, d, d + kDay);
                n += 1.0;
            }
            return s / n;
        };
        const double daily_run_c = daily_max_mean(closed, w, end + 1);
        const double daily_run_v = daily_max_mean(vented, w, end + 1);
        WARN(std::format("S26 daily longest wet run mean closed/vented {:.1f}/{:.1f} h", daily_run_c, daily_run_v));
        CHECK(daily_run_c > daily_run_v);
        CHECK(bot_c > bot_v);
        CHECK(rot_c > rot_v);
        check_conserved("s26_closed", closed);
        check_conserved("s26_vented", vented);
    }

    // ---- S27 유동팬 ----

    // S27: 팬 쪽 젖은 시간·감염·썩은 과실이 작고 하우스 습도는 거의 같음 (완료 기준 "공기 흐름이 젖음을 줄여 병을 줄임").
    // "거의 같음"은 B3의 하우스 습도 차 범위(0~3%p) 안으로 봐요. 참고 목표값 B3: 젖은 시간 40~70% 감소
    // (6차 썩은 과실 10.5% → 4.5%).
    SECTION("S27 fan cuts leaf wetness and rot without changing house humidity") {
        const Run& closed = m5b("s26_closed", kS26Ticks);
        const Run& fan = m5b("s27_closed_fan", kS26Ticks);
        const std::size_t w = kS26Window;
        const std::size_t end = kS26Ticks;
        const double rh_c = mean_range(closed.rh, w, end + 1);
        const double rh_f = mean_range(fan.rh, w, end + 1);
        const double wet_c = sum_range(closed.wet_frac, w + 1, end + 1);
        const double wet_f = sum_range(fan.wet_frac, w + 1, end + 1);
        const double bot_c = mean_range(closed.botrytis, w, end + 1);
        const double bot_f = mean_range(fan.botrytis, w, end + 1);
        const double rot_c = rot_ratio(closed, w, end);
        const double rot_f = rot_ratio(fan, w, end);
        WARN(std::format("S27 closed/fan: rh {:.4f}/{:.4f}, wet h {:.0f}/{:.0f} ({:+.0f}%), botrytis {:.4f}/{:.4f}, "
                         "rot {:.1f}%/{:.1f}%",
                         rh_c, rh_f, wet_c, wet_f, -100 * reduction(wet_c, wet_f), bot_c, bot_f, 100 * rot_c, 100 * rot_f));
        CHECK(wet_f < wet_c);
        CHECK(bot_f < bot_c);
        CHECK(rot_f < rot_c);
        CHECK(std::abs(rh_f - rh_c) <= 0.03);
        check_conserved("s27_closed_fan", fan);
    }
}

// ---- 습도만 보는 변형 (12/1 정식 직후, 62일) ----

// S26b: 난방 12 °C 쪽 밤 습도·젖은 시간이 작고 난방 에너지가 큼.
// 펄스(01~05시만 12 °C)는 같은 시각 대조보다 습도가 낮음 (참고 목표값 B13 −10~−25%p, 3차 −16~−18%p).
TEST_CASE("M5b S26b heating lowers night humidity and wet hours at higher energy", "[scenario][m5b][s26][s26b]") {
    const Run& control = humidity_control();
    const Run& heated = m5b("s26_heated", kHumidityTicks);
    const Run& pulse = m5b("s26_heat_pulse", kHumidityTicks);
    const std::size_t end = kHumidityTicks + 1;
    const double night_c = mean_hours(control, control.rh, 1, end, night_hour);
    const double night_h = mean_hours(heated, heated.rh, 1, end, night_hour);
    const double wet_c = sum_range(control.wet_frac, 1, end);
    const double wet_h = sum_range(heated.wet_frac, 1, end);
    const auto pulse_hours = [](int h) { return h >= 2 && h <= 5; };  // 01~04시를 처리한 뒤의 스냅샷
    const double pulse_c = mean_hours(control, control.rh, 1, end, pulse_hours);
    const double pulse_p = mean_hours(pulse, pulse.rh, 1, end, pulse_hours);
    WARN(std::format("S26b control 6C/heated 12C: night rh {:.3f}/{:.3f}, wet h {:.0f}/{:.0f}, heating {:.1f}/{:.1f} MJ; "
                     "pulse 01-05h rh {:.3f} vs control {:.3f} ({:+.1f}%p)",
                     night_c, night_h, wet_c, wet_h, control.heating_MJ.back(), heated.heating_MJ.back(), pulse_p,
                     pulse_c, 100 * (pulse_p - pulse_c)));
    CHECK(night_h < night_c);
    CHECK(wet_h < wet_c);
    CHECK(heated.heating_MJ.back() > control.heating_MJ.back());
    CHECK(pulse_p < pulse_c);
    check_conserved("humidity control 6C", control);
    check_conserved("s26_heated", heated);
    check_conserved("s26_heat_pulse", pulse);
}

// S26c: 4겹 쪽 습도가 높고 결로가 작음 (참고 목표값 B14 방향만, 3차 22~5시 0.888 < 0.925 < 0.945, 결로 90 > 82 > 72 mm)
TEST_CASE("M5b S26c four layers are more humid and condense less than two", "[scenario][m5b][s26][s26c]") {
    const Run& two = m5b("s26_layers2", kHumidityTicks);
    const Run& four = m5b("s26_layers4", kHumidityTicks);
    const Run& three = humidity_control();
    const std::size_t end = kHumidityTicks + 1;
    const double rh2 = mean_hours(two, two.rh, 1, end, night_hour);
    const double rh3 = mean_hours(three, three.rh, 1, end, night_hour);
    const double rh4 = mean_hours(four, four.rh, 1, end, night_hour);
    WARN(std::format("S26c 2/3/4 layers: night rh {:.3f}/{:.3f}/{:.3f}, condensed {:.1f}/{:.1f}/{:.1f} mm", rh2, rh3, rh4,
                     two.condensed_mm.back(), three.condensed_mm.back(), four.condensed_mm.back()));
    CHECK(rh4 > rh2);
    CHECK(four.condensed_mm.back() < two.condensed_mm.back());
    // 3겹은 그 사이 (같은 6 °C 밀폐)
    CHECK(rh2 < rh3);
    CHECK(rh3 < rh4);
    check_conserved("s26_layers2", two);
    check_conserved("s26_layers4", four);
}

// S26d: 저녁 환기(16~18시 창 전부 1.0) 쪽 8 h 이상 젖은 밤이 적음 (참고 목표값 B15 "절반 이하", 3차 13 대 61)
TEST_CASE("M5b S26d evening venting cuts long wet nights", "[scenario][m5b][s26][s26d]") {
    const Run& control = humidity_control();
    const Run& evening = m5b("s26_evening_vent", kHumidityTicks);
    const std::size_t end = kHumidityTicks + 1;
    const std::size_t nights_c = long_wet_nights(control, 1, end, 8);
    const std::size_t nights_e = long_wet_nights(evening, 1, end, 8);
    const double wet_c = sum_range(control.wet_frac, 1, end);
    const double wet_e = sum_range(evening.wet_frac, 1, end);
    WARN(std::format("S26d closed/evening vent: nights wet >= 8 h {}/{}, wet h {:.0f}/{:.0f}", nights_c, nights_e, wet_c,
                     wet_e));
    CHECK(nights_e < nights_c);
    CHECK(wet_e < wet_c);
    check_conserved("s26_evening_vent", evening);
}

// ---- S28 점적 대 두상 관수 (등급 C, 90일) ----

// S28: 발병 포기 두상 > 점적, 필름 + 두상 > 맨흙 + 두상(포기 → 포기 물 튐), 발병 즉시 제거 쪽은 뒤따르는 발병·고사가 적음.
// 두상 관수 쪽은 관수 틱마다 잎 젖음 1(overhead_wet_index)이고 젖은 시간이 큼 (house-humidity 8장 S28).
// 참고 목표값: B6 90일 발병 포기 2~4배(5차 62 대 17, 3.6배), 점적 쪽 고사 2~10%(7.5%), B16 잠복 모종 고사 5~30%.
// s28_overhead_remove는 s28_overhead와 같은 시나리오에 sim-cli --remove-on-onset anthracnose 동작을 붙여요
// (탄저병 발병 이벤트가 난 칸에 다음 틱 시작 전 RemovePlant).
TEST_CASE("M5b S28 overhead irrigation spreads anthracnose and removal limits it", "[scenario][m5b][s28]") {
    const Run& drip = m5b("s28_drip", kS28Ticks);
    const Run& overhead = m5b("s28_overhead", kS28Ticks);
    const Run& bare = m5b("s28_overhead_bare", kS28Ticks);
    const Run& removed = run_scenario("s28_overhead_remove+remove_on_onset",
                                      RunSpec{.path = "m5b/s28_overhead_remove",
                                              .ticks = kS28Ticks,
                                              .remove_on_onset = sim::FoliarDisease::Anthracnose});
    const double cells = static_cast<double>(drip.cells);
    WARN(std::format("S28 anthracnose onsets drip/overhead/bare/remove {}/{}/{}/{}, deaths {}/{}/{}/{} "
                     "(drip death {:.1f}%), removed {}, wet h {:.0f}/{:.0f}",
                     drip.onsets[kAnth], overhead.onsets[kAnth], bare.onsets[kAnth], removed.onsets[kAnth], drip.died,
                     overhead.died, bare.died, removed.died, 100.0 * static_cast<double>(drip.died) / cells,
                     removed.ledger.plants_removed, sum_range(drip.wet_frac, 1, kS28Ticks + 1),
                     sum_range(overhead.wet_frac, 1, kS28Ticks + 1)));
    CHECK(overhead.onsets[kAnth] > drip.onsets[kAnth]);
    CHECK(overhead.onsets[kAnth] > bare.onsets[kAnth]);
    CHECK(removed.ledger.plants_removed > 0);
    CHECK(removed.onsets[kAnth] < overhead.onsets[kAnth]);
    CHECK(removed.died < overhead.died);
    CHECK(sum_range(overhead.wet_frac, 1, kS28Ticks + 1) > sum_range(drip.wet_frac, 1, kS28Ticks + 1));
    // 두상 관수 틱마다 살아 있는 모든 포기의 잎 젖음 = overhead_wet_index, 점적 쪽은 두상 표시가 없음
    std::size_t overhead_ticks = 0;
    for (std::size_t i = 1; i <= kS28Ticks; ++i) {
        CHECK(drip.overhead_frac[i] == 0.0);
        if (overhead.overhead_frac[i] > 0.0) {
            ++overhead_ticks;
            CHECK(overhead.overhead_frac[i] == 1.0);  // 관수가 하우스 전체
            CHECK(overhead.leaf_wet_min[i] >= overhead.overhead_wet);
        }
    }
    CHECK(overhead_ticks >= 80);  // 매일 08시 관수 (90일)
    for (const auto& [name, r] : {std::pair<const char*, const Run*>{"s28_drip", &drip}, {"s28_overhead", &overhead},
                                  {"s28_overhead_bare", &bare}, {"s28_overhead_remove", &removed}}) {
        check_conserved(name, *r);
    }
}

// ---- S29 질소 과다 (시즌 263일) ----

// S29: 과다 쪽 탄저·잿빛곰팡이·흰가루 감염과 손실(썩은 과실·흰가루 과실)이 큼.
// 참고 목표값 B21 1.2~2배(4차 흰가루 2.3배, 썩음 6.3% 대 3.7%, 탄저 1.3배).
// B17 탄저병 겨울 정지: s29_standard의 12~2월 감염 증가가 9~11월보다 작음 (목표 1/5 이하, 4차 1/24).
TEST_CASE("M5b S29 nitrogen excess raises foliar infection and losses", "[scenario][m5b][s29]") {
    const Run& standard = m5b("s29_standard", kSeasonTicks);
    const Run& excess = m5b("s29_n_excess", kSeasonTicks);
    const std::size_t end = kSeasonTicks + 1;
    const double anth_s = mean_range(standard.anthracnose, 0, end);
    const double anth_e = mean_range(excess.anthracnose, 0, end);
    const double bot_s = mean_range(standard.botrytis, 0, end);
    const double bot_e = mean_range(excess.botrytis, 0, end);
    const double pm_s = mean_range(standard.mildew, 0, end);
    const double pm_e = mean_range(excess.mildew, 0, end);
    const double rot_s = rot_ratio(standard, 0, kSeasonTicks);
    const double rot_e = rot_ratio(excess, 0, kSeasonTicks);
    const auto mildew_ratio = [](const Run& r) {
        const double m = r.mildew_fruit_count.back();
        const double h = r.harvest_count.back();
        return m + h > 0.0 ? m / (m + h) : 0.0;
    };
    // 9/10 정식(day 253): 12/1 = 틱 1968, 3/1 = 틱 4104
    constexpr std::size_t kDec1 = 1968;
    constexpr std::size_t kMar1 = 4104;
    const double autumn = delta(standard.anthracnose, 0, kDec1);
    const double winter = delta(standard.anthracnose, kDec1, kMar1);
    WARN(std::format("S29 standard/excess: anthracnose {:.4f}/{:.4f}, botrytis {:.4f}/{:.4f}, mildew {:.4f}/{:.4f}, "
                     "rot {:.1f}%/{:.1f}%, mildew fruit {:.1f}%/{:.1f}%; B17 winter/autumn anthracnose rise "
                     "{:.5f}/{:.5f}",
                     anth_s, anth_e, bot_s, bot_e, pm_s, pm_e, 100 * rot_s, 100 * rot_e, 100 * mildew_ratio(standard),
                     100 * mildew_ratio(excess), winter, autumn));
    CHECK(anth_e > anth_s);
    CHECK(bot_e > bot_s);
    CHECK(pm_e > pm_s);
    CHECK(rot_e > rot_s);
    CHECK(mildew_ratio(excess) > mildew_ratio(standard));
    CHECK(winter < autumn);
    check_conserved("s29_standard", standard);
    check_conserved("s29_n_excess", excess);
}

// ---- S30 기온 (측정 창 4/1~5/15) ----

// S30: 따뜻한 쪽 응애 증가가 빠르고 광합성 피해(응애 2주 평균 밀도의 함수)가 큼.
// 참고 목표값 B7 하루 r 0.11~0.19(25 °C) / 0.03~0.08(겨울 변온), B19 방치 피해 10~40%.
// 명세 9장: 두 쪽 습도 평균은 맞추지 못했어요(1.00 대 0.95, 미해결) — 습도는 검사하지 않아요.
TEST_CASE("M5b S30 warmer house grows spider mites faster", "[scenario][m5b][s30]") {
    const Run& hot = m5b("s30_hot", kSpringTicks);
    const Run& cool = m5b("s30_cool", kSpringTicks);
    const std::size_t w = kS30Window;
    const std::size_t end = kSpringTicks;
    const auto midday = [](int h) { return h >= 12 && h <= 16; };  // 11~15시를 처리한 뒤의 스냅샷
    const double t_hot = mean_hours(hot, hot.air_temp, w, end + 1, midday);
    const double t_cool = mean_hours(cool, cool.air_temp, w, end + 1, midday);
    const double rise_hot = hot.house_mite[end] - hot.house_mite[w];
    const double rise_cool = cool.house_mite[end] - cool.house_mite[w];
    WARN(std::format("S30 hot/cool: midday {:.1f}/{:.1f} C, house mite {:.3f}->{:.3f} / {:.3f}->{:.3f}, "
                     "2-week mean at end {:.3f}/{:.3f}",
                     t_hot, t_cool, hot.house_mite[w], hot.house_mite[end], cool.house_mite[w], cool.house_mite[end],
                     hot.mite_mean_2w[end], cool.mite_mean_2w[end]));
    REQUIRE(t_hot > t_cool);  // 시나리오 전제
    CHECK(rise_hot > rise_cool);
    CHECK(hot.mite_mean_2w[end] > cool.mite_mean_2w[end]);
    check_conserved("s30_hot", hot);
    check_conserved("s30_cool", cool);
}

// ---- S31 천적 (방사량 75마리/m², 2026-11-20 디렉터 결정으로 결정 46의 보류를 풀었어요) ----

namespace {
// 방사 tick 뒤 weeks주의 하우스 평균 응애 지수
double mite_at(const Run& r, std::uint64_t release_tick, std::uint64_t weeks) {
    return r.house_mite[release_tick + weeks * kWeek];
}
// 대조 대비 감소율
double mite_reduction(const Run& control, const Run& treated, std::uint64_t release_tick, std::uint64_t weeks) {
    return reduction(mite_at(control, release_tick, weeks), mite_at(treated, release_tick, weeks));
}
// 작기 끝 누적 수확(정상과 생체중) 증가율 (대조 대비)
double harvest_gain(const Run& control, const Run& treated) {
    return -reduction(control.harvest_fresh.back(), treated.harvest_fresh.back());
}
}  // namespace

// S31 B8: 일찍 방사(11/25, 응애 약 0.045)는 방사 +3·+4·+5주에 방사 없음 대비 응애를 60% 이상 줄여요
// (목표값 B8 "3~5주에 대조 대비 60% 이상", 디렉터 승인으로 수치 검사). +6주도 대조보다 작음.
// 측정(75마리/m²): +3/+5/+6주 72.3/77.1/78.0%.
TEST_CASE("M5b S31 predator release controls spider mites", "[scenario][m5b][s31]") {
    SECTION("early release cuts mites by at least 60 percent in weeks 3 to 5") {
        const Run& none = m5b("s31_none", kS31Ticks);
        const Run& early = m5b("s31_predator", kS31Ticks);
        std::string line;
        for (std::uint64_t k = 3; k <= 6; ++k) {
            const double red = mite_reduction(none, early, kS31Early, k);
            line += std::format(" +{}w {:.3f}/{:.3f} ({:.1f}%)", k, mite_at(none, kS31Early, k), mite_at(early, kS31Early, k),
                                100 * red);
            if (k <= 5) {
                CHECK(red >= 0.6);
            }
            CHECK(mite_at(early, kS31Early, k) < mite_at(none, kS31Early, k));
        }
        WARN("S31 none/early house mite:" + line + std::format(", released {:.0f}/m2", early.ledger.predators_released_per_m2));
        CHECK(early.ledger.predators_released_per_m2 > 0.0);
        check_conserved("s31_none", none);
        check_conserved("s31_predator", early);
    }

    // S31 B20(새 정의): 늦은 방사(2/2, 응애 약 0.17)도 응애를 잡지만(+5주에 대조보다 작음), 작기 끝 수확 증가는
    // 일찍 방사보다 작아요 ("늦게 방사하면 응애는 잡지만 그 전 손실은 못 되돌린다").
    // 측정(75마리/m²): 늦은 방사 +3/+5/+6주 83.6/93.9/94.5%, 수확 증가 일찍 +5.2% 대 늦게 +2.3%.
    SECTION("late release controls mites but recovers less harvest than early release") {
        const Run& none = m5b("s31_none", kS31Ticks);
        const Run& early = m5b("s31_predator", kS31Ticks);
        const Run& late = m5b("s31_predator_late", kS31Ticks);
        const double gain_early = harvest_gain(none, early);
        const double gain_late = harvest_gain(none, late);
        WARN(std::format("S31 late: mite at release {:.3f}, +3/+5/+6w reduction {:.1f}/{:.1f}/{:.1f}%; harvest gain vs none "
                         "early {:+.1f}% late {:+.1f}%",
                         mite_at(late, kS31Late, 0), 100 * mite_reduction(none, late, kS31Late, 3),
                         100 * mite_reduction(none, late, kS31Late, 5), 100 * mite_reduction(none, late, kS31Late, 6),
                         100 * gain_early, 100 * gain_late));
        // 늦은 방사 하우스는 2/2까지 방사가 없어서 방사 없음과 같고, 그때 응애가 이미 많아요
        CHECK(mite_at(late, kS31Late, 0) == mite_at(none, kS31Late, 0));
        CHECK(mite_at(late, kS31Late, 0) > mite_at(early, kS31Early, 0));
        CHECK(mite_at(late, kS31Late, 5) < mite_at(none, kS31Late, 5));
        CHECK(gain_late > 0.0);
        CHECK(gain_early > gain_late);
        check_conserved("s31_predator_late", late);
    }

    // S31 B9 건조: 같은 건조 기상·관리의 방사 없음 대조(s31_none_dry)보다 응애가 작고(방사 후 3~6주), 대조 대비 감소율은
    // +5주가 +3주보다 작고(건조해 천적이 늘지 못해 방제가 약해짐) 습한 쌍(s31_predator 대 s31_none)의 +5주보다 작아요.
    // 측정(75마리/m²): 건조 쌍 +3/+5/+6주 81.9/61.6/43.8%, 습한 쌍 72.3/77.1/78.0%.
    SECTION("dry air weakens predator control") {
        const Run& none = m5b("s31_none", kS31Ticks);
        const Run& early = m5b("s31_predator", kS31Ticks);
        const Run& none_dry = m5b("s31_none_dry", kS31Ticks);
        const Run& dry = m5b("s31_predator_dry", kS31Ticks);
        const double red3 = mite_reduction(none_dry, dry, kS31Early, 3);
        const double red5 = mite_reduction(none_dry, dry, kS31Early, 5);
        const double red6 = mite_reduction(none_dry, dry, kS31Early, 6);
        const double humid5 = mite_reduction(none, early, kS31Early, 5);
        WARN(std::format("S31 dry pair +3/+5/+6w reduction {:.1f}/{:.1f}/{:.1f}% (humid pair +5w {:.1f}%), "
                         "window rh mean dry {:.2f}",
                         100 * red3, 100 * red5, 100 * red6, 100 * humid5,
                         mean_range(dry.rh_mean, kS31Early, kS31Early + 6 * kWeek + 1)));
        for (std::uint64_t k = 3; k <= 6; ++k) {
            CHECK(mite_at(dry, kS31Early, k) < mite_at(none_dry, kS31Early, k));
        }
        CHECK(red5 < red3);
        CHECK(red5 < humid5);
        check_conserved("s31_none_dry", none_dry);
        check_conserved("s31_predator_dry", dry);
    }
}

// ---- S32 흰가루병 (측정 창 3/1~5/15) ----

// S32: 흰가루병 두상 관수 < 점적, 환기·건조 < 기본. 4겹은 잎 젖음·잿빛곰팡이가 큼 (결정 39, 이전 "4겹 ≥ 2겹" 삭제).
// 참고 목표값 B10: 무방제 끝 잎 면적 10~25%, 흰가루 과실 1~5% (6차 bright 0.216, 두상 0.193, 건조 0.200,
// 4겹 0.161·썩음 5.4% 대 2.6%).
TEST_CASE("M5b S32 powdery mildew is lower with overhead water or dry air", "[scenario][m5b][s32]") {
    const Run& bright = m5b("s32_bright", kSpringTicks);
    const Run& dark = m5b("s32_dark", kSpringTicks);
    const Run& overhead = m5b("s32_overhead", kSpringTicks);
    const Run& dry = m5b("s32_dry", kSpringTicks);
    const std::size_t w = kS32Window;
    const std::size_t end = kSpringTicks;
    const double wet_b = mean_range(bright.leaf_wet_mean, w, end + 1);
    const double wet_d = mean_range(dark.leaf_wet_mean, w, end + 1);
    const double bot_b = mean_range(bright.botrytis, w, end + 1);
    const double bot_d = mean_range(dark.botrytis, w, end + 1);
    WARN(std::format("S32 mildew end bright/dark/overhead/dry {:.3f}/{:.3f}/{:.3f}/{:.3f}; leaf wetness bright/dark "
                     "{:.3f}/{:.3f}, botrytis {:.4f}/{:.4f}, rot {:.1f}%/{:.1f}%, window rh mean dry {:.2f}",
                     bright.mildew[end], dark.mildew[end], overhead.mildew[end], dry.mildew[end], wet_b, wet_d, bot_b,
                     bot_d, 100 * rot_ratio(bright, w, end), 100 * rot_ratio(dark, w, end),
                     mean_range(dry.rh_mean, w, end + 1)));
    CHECK(overhead.mildew[end] < bright.mildew[end]);
    CHECK(dry.mildew[end] < bright.mildew[end]);
    CHECK(wet_d > wet_b);
    CHECK(bot_d > bot_b);
    CHECK(rot_ratio(dark, w, end) > rot_ratio(bright, w, end));
    for (const auto& [name, r] : {std::pair<const char*, const Run*>{"s32_bright", &bright}, {"s32_dark", &dark},
                                  {"s32_overhead", &overhead}, {"s32_dry", &dry}}) {
        check_conserved(name, *r);
    }
}

// ---- S34 배경 포자 시즌 (결정 45, 방향만, 회귀 게이트 밖) ----

// S34: 배경 포자 쪽 누적 수확이 작고 썩은 과실·흰가루 과실이 많음. 줄어드는 폭은 질소 과다 쪽이 더 큼.
// 측정(참고): season_standard 대비 −6.6%, season_n_excess 대비 −17.9%.
TEST_CASE("M5b S34 background spores cut season harvest more under nitrogen excess", "[scenario][m5b][s34]") {
    const Run& standard = run_scenario("m4/season_standard", RunSpec{.path = "m4/season_standard", .ticks = kSeasonTicks});
    const Run& excess = run_scenario("m4/season_n_excess", RunSpec{.path = "m4/season_n_excess", .ticks = kSeasonTicks});
    const Run& spores = m5b("s34_season_spores", kSeasonTicks);
    const Run& spores_excess = m5b("s34_season_spores_n_excess", kSeasonTicks);
    const double loss_std = reduction(standard.harvest_fresh.back(), spores.harvest_fresh.back());
    const double loss_exc = reduction(excess.harvest_fresh.back(), spores_excess.harvest_fresh.back());
    WARN(std::format("S34 harvest g/m2 standard {:.0f} -> spores {:.0f} ({:+.1f}%), n_excess {:.0f} -> {:.0f} ({:+.1f}%); "
                     "rot {:.0f}->{:.0f} / {:.0f}->{:.0f}, mildew fruit {:.0f}->{:.0f} / {:.0f}->{:.0f}",
                     standard.harvest_fresh.back(), spores.harvest_fresh.back(), -100 * loss_std,
                     excess.harvest_fresh.back(), spores_excess.harvest_fresh.back(), -100 * loss_exc,
                     standard.rot_count.back(), spores.rot_count.back(), excess.rot_count.back(),
                     spores_excess.rot_count.back(), standard.mildew_fruit_count.back(),
                     spores.mildew_fruit_count.back(), excess.mildew_fruit_count.back(),
                     spores_excess.mildew_fruit_count.back()));
    CHECK(spores.harvest_fresh.back() < standard.harvest_fresh.back());
    CHECK(spores_excess.harvest_fresh.back() < excess.harvest_fresh.back());
    CHECK(spores.rot_count.back() > standard.rot_count.back());
    CHECK(spores_excess.rot_count.back() > excess.rot_count.back());
    CHECK(spores.mildew_fruit_count.back() > standard.mildew_fruit_count.back());
    CHECK(spores_excess.mildew_fruit_count.back() > excess.mildew_fruit_count.back());
    CHECK(loss_exc > loss_std);
    for (const auto& [name, r] :
         {std::pair<const char*, const Run*>{"m4 season_standard", &standard}, {"m4 season_n_excess", &excess},
          {"s34_season_spores", &spores}, {"s34_season_spores_n_excess", &spores_excess}}) {
        check_conserved(name, *r);
    }
}
