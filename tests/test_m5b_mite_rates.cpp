// M5b 측정 하네스 (7차에서 만듦) (foliar-disease 4.2·4.5, 7장 B7·B8): 응애·칠레이리응애의 하루 내적 증가율 r과
// 병든 포기 아래 탄저병 흙 접종원을 일정 조건에서 재요. 숨긴 테스트라 ctest에는 안 잡히고, 직접 실행해요:
//   build\ci\tests\Release\sim-tests.exe "[harness]"
// 파라미터는 저장소 data/params.toml (로더로 읽음). 환경 변수 SIM_HARNESS_PARAMS가 있으면 그 파일을 읽어요(값 비교용).

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "sim/processes.hpp"
#include "simdata/loader.hpp"
#include "test_helpers.hpp"

namespace process = sim::process;

namespace {

constexpr double kStart = 1e-4;  // 낮은 밀도 (로지스틱 항이 무시될 만큼)
constexpr int kDays = 10;

sim::Params load_harness_params() {
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996)  // getenv: 테스트 도구 전용 (코어 밖), 읽기만
#endif
    const char* override_path = std::getenv("SIM_HARNESS_PARAMS");
#ifdef _MSC_VER
#pragma warning(pop)
#endif
    const auto path = override_path != nullptr ? std::filesystem::path(override_path)
                                               : test::repo_path("data/params.toml");
    auto loaded = simdata::load_params(path);
    REQUIRE(loaded.has_value());
    return *loaded;
}

void step(sim::Crop& crop, const sim::Params& p, double air_degC, double rh_mean) {
    process::CropEnvironment env = test::crop_env(0.0, air_degC);
    env.house_rh_mean_index = rh_mean;
    env.house_spider_mite_mean_index = crop.spider_mite_index;  // 이동 0
    process::FoliarOnsets onsets{};
    process::update_mites(crop, p.foliar_disease, env, 0, onsets);
}

// 천적 없음, 응애 낮은 밀도: 하루 r = ln(M_end / M0) / 일수
double mite_r(const sim::Params& p, const std::function<double(int)>& temp_at_hour, double rh_mean) {
    sim::Crop crop{};
    crop.spider_mite_index = kStart;
    for (int h = 0; h < kDays * 24; ++h) {
        step(crop, p, temp_at_hour(h), rh_mean);
    }
    return std::log(crop.spider_mite_index / kStart) / kDays;
}

// 먹이 응애를 prey로 매 틱 되돌려 두고(먹이 일정), 천적 낮은 밀도에서 하루 r
double predator_r(const sim::Params& p, const std::function<double(int)>& temp_at_hour, double rh_mean, double prey) {
    sim::Crop crop{};
    crop.predator_mite_index = kStart;
    for (int h = 0; h < kDays * 24; ++h) {
        crop.spider_mite_index = prey;
        step(crop, p, temp_at_hour(h), rh_mean);
    }
    return std::log(crop.predator_mite_index / kStart) / kDays;
}

std::function<double(int)> constant(double t) {
    return [t](int) { return t; };
}

// 겨울 변온: 밤 6 °C 12시간, 낮 22 °C 12시간
double winter_square(int h) { return (h % 24) < 12 ? 6.0 : 22.0; }
// 겨울 변온 (사인): 최저 6, 최고 22, 14시 최고
double winter_sine(int h) {
    const double pi = std::acos(-1.0);
    return 14.0 + 8.0 * std::cos(2.0 * pi * (static_cast<double>(h % 24) - 14.0) / 24.0);
}

}  // namespace

// 하네스: 일정 기온별 응애·천적 하루 r 표 (기대값 없음, 출력만)
TEST_CASE("Harness mite and predator intrinsic growth per day", "[.][harness][m5b]") {
    const sim::Params p = load_harness_params();
    const double temps[] = {8.0, 10.0, 12.0, 15.0, 18.0, 20.0, 22.0, 25.0, 28.0, 30.0};
    std::printf("\n T_degC | mite r RH0.7 | pred r prey0.4 RH0.8 | ratio | pred prey0.4 RH0.55 | pred prey0.1 | "
                "pred prey0.045 | pred prey0.045 RH0.55\n");
    for (double t : temps) {
        const double m = mite_r(p, constant(t), 0.7);
        const double d = predator_r(p, constant(t), 0.8, 0.4);
        std::printf(" %6.1f | %12.4f | %20.4f | %5.2f | %19.4f | %12.4f | %14.4f | %21.4f\n", t, m, d,
                    m > 0.0 ? d / m : 0.0, predator_r(p, constant(t), 0.55, 0.4), predator_r(p, constant(t), 0.8, 0.1),
                    predator_r(p, constant(t), 0.8, 0.045), predator_r(p, constant(t), 0.55, 0.045));
    }
    std::printf(" winter 6/22 square: mite r %.4f (RH0.7), pred r prey0.4 %.4f, prey0.1 %.4f, prey0.045 %.4f\n",
                mite_r(p, winter_square, 0.7), predator_r(p, winter_square, 0.8, 0.4),
                predator_r(p, winter_square, 0.8, 0.1), predator_r(p, winter_square, 0.8, 0.045));
    std::printf(" winter 6/22 sine:   mite r %.4f (RH0.7), pred r prey0.4 %.4f, prey0.1 %.4f, prey0.045 %.4f\n",
                mite_r(p, winter_sine, 0.7), predator_r(p, winter_sine, 0.8, 0.4),
                predator_r(p, winter_sine, 0.8, 0.1), predator_r(p, winter_sine, 0.8, 0.045));
    SUCCEED();
}

// 하네스: 감염 지수 일정한 살아 있는 포기 아래 흙 탄저병 접종원 (0에서 시작), 길항균 0 / 0.5(이입 유지), 흙 20·27 °C
TEST_CASE("Harness anthracnose soil inoculum under a diseased plant", "[.][harness][m5b]") {
    const sim::Params p = load_harness_params();
    const std::size_t g = sim::microbe(sim::MicrobeGroup::Anthracnose);
    const std::size_t ga = sim::microbe(sim::MicrobeGroup::Antagonist);
    std::printf("\n infection | soil_degC | antagonist | anthracnose index day 7 / 14 / 28 / 56\n");
    for (double inf : {0.0, 0.3, 0.5, 0.8}) {
        for (double soil_degC : {20.0, 27.0}) {
            for (double antagonist : {0.0, 0.5}) {
                sim::Params q = p;
                if (antagonist == 0.0) {
                    q.microbes.groups[ga].immigration_rate_per_hour = 0.0;
                }
                sim::SoilCell cell = test::test_cell();
                cell.soil_temp_degC = soil_degC;
                cell.microbe_index[g] = 0.0;
                cell.microbe_index[ga] = antagonist;
                process::MicrobeHost host;
                host.root_g_per_m2 = 12.0;
                host.anthracnose_infection_index = inf;
                double at[4] = {};
                for (int h = 1; h <= 56 * 24; ++h) {
                    cell.soil_temp_degC = soil_degC;
                    process::update_microbes(cell, q, host);
                    if (h == 7 * 24) at[0] = cell.microbe_index[g];
                    if (h == 14 * 24) at[1] = cell.microbe_index[g];
                    if (h == 28 * 24) at[2] = cell.microbe_index[g];
                    if (h == 56 * 24) at[3] = cell.microbe_index[g];
                }
                std::printf(" %9.2f | %9.1f | %10.2f | %.4f / %.4f / %.4f / %.4f (antagonist end %.2f)\n", inf,
                            soil_degC, antagonist, at[0], at[1], at[2], at[3], cell.microbe_index[ga]);
            }
        }
    }
    SUCCEED();
}
