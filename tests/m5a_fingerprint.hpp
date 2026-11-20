#pragma once

// M5a 비트 동일성 지문 (docs/spec/foliar-disease.md 9장 "등급 A ... 결과가 M5a와 비트 단위로 같음").
// M5a 커밋(d6dff07)과 지금 코드 양쪽에 있는 필드만 FNV-1a로 해시해요. 같은 소스를 M5a 코드에 붙여 빌드해
// 기대값을 기록했어요 (tests/test_foliar_disease.cpp). 새 필드(지상부 상태, 탄저병 무리, 습도)는 넣지 않아요.
// 시나리오: test_params() + 포기·병원균 차이 켬, 2x3, 9/17 정식(등급 A, 활착 0), 시들음병균 0.3, 흑색 필름,
// 비료 1회, 2 mm 관수 매일, 120일.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "sim/world.hpp"
#include "test_helpers.hpp"

namespace test::m5a_fingerprint {

inline constexpr std::size_t kM5aMicrobeGroups = 7;  // M5a 무리 수 (탄저병 무리 제외)
inline constexpr std::uint32_t kTicks = 24 * 120;

inline sim::Params params() {
    sim::Params p = test::test_params();
    p.phenology.induction_spread_index = 0.4;
    p.microbes.susceptibility_spread_index = 0.5;
    p.microbes.pathogen_spread_index = 0.5;
    return p;
}

inline sim::Scenario scenario() {
    sim::Scenario s = test::test_scenario(260, 2, 3);
    s.soil.microbe_index[sim::microbe(sim::MicrobeGroup::Fusarium)] = 0.3;
    s.crop = sim::CropInit{.seedling_grade = "A", .plants_per_m2 = 8.0, .initial_establishment_index = 0.0};
    return s;
}

class Hasher {
public:
    void f(double v) {
        unsigned char bytes[sizeof(double)];
        std::memcpy(bytes, &v, sizeof(double));
        for (unsigned char b : bytes) {
            hash_ ^= b;
            hash_ *= 1099511628211ULL;
        }
    }
    void u(std::uint64_t v) { f(static_cast<double>(v)); }
    [[nodiscard]] std::uint64_t value() const { return hash_; }

private:
    std::uint64_t hash_ = 14695981039346656037ULL;
};

inline void add_snapshot(Hasher& h, const sim::Snapshot& s) {
    h.u(s.tick);
    h.f(s.house.air_temp_degC);
    h.f(s.house.air_temp_mean_degC);
    h.f(s.house.co2_ppm);
    h.f(s.house.heating_energy_MJ_per_m2);
    for (std::size_t i = 0; i < s.soil.size(); ++i) {
        const sim::SoilCell& c = s.soil[i];
        for (double v : {c.water_mm, c.soil_temp_degC, c.deep_soil_temp_degC, c.labile_C_g_per_m2, c.labile_N_g_per_m2,
                         c.stable_C_g_per_m2, c.stable_N_g_per_m2, c.nh4_N_g_per_m2, c.no2_N_g_per_m2,
                         c.no3_N_g_per_m2, c.ph, c.k_g_per_m2, c.ca_g_per_m2, c.mg_g_per_m2, c.p_g_per_m2,
                         c.s_g_per_m2, c.mn_g_per_m2, c.mn_oxide_g_per_m2, c.p_fixed_g_per_m2, c.ec_dS_per_m}) {
            h.f(v);
        }
        for (std::size_t g = 0; g < kM5aMicrobeGroups; ++g) {
            h.f(c.microbe_index[g]);
            h.f(c.disinfection_kill_index[g]);
        }
        for (double r : c.fumigant_residue_index) {
            h.f(r);
        }
        const sim::Crop& k = s.crops[i];
        h.u(k.present ? 1 : 0);
        h.u(k.alive ? 1 : 0);
        for (double v : {k.leaf_g_per_m2, k.root_g_per_m2, k.crown_g_per_m2, k.root_health, k.plant_N_g_per_m2,
                         k.plant_P_g_per_m2, k.plant_K_g_per_m2, k.plant_Ca_g_per_m2, k.plant_Mg_g_per_m2,
                         k.plant_Mn_g_per_m2, k.induction_index, k.establishment_index, k.reserve_g_per_m2,
                         k.rgr_mean_per_day, k.size_satisfaction_mean, k.infection_index[0], k.infection_index[1],
                         k.disease_susceptibility_factor, k.last_gross_g_per_m2, k.fruit_g_per_m2(),
                         k.harvest.harvested_fresh_g, k.harvest.harvested_deformed_fresh_g}) {
            h.f(v);
        }
        h.u(k.fruits.size());
        h.u(k.trusses.size());
        h.u(k.harvest.harvested_fruits);
        h.u(k.harvest.harvested_deformed);
        h.u(k.harvest.aborted_fruits);
    }
    const sim::Ledger& l = s.ledger;
    for (double v : {l.water_in_mm, l.water_evaporated_mm, l.water_drained_mm, l.N_in_g, l.N_leached_g, l.N_gas_g,
                     l.C_in_g, l.C_respired_g, l.water_transpired_mm, l.N_uptake_g, l.P_uptake_g, l.K_uptake_g,
                     l.Ca_uptake_g, l.Mg_uptake_g, l.Mn_uptake_g, l.H_eq_net, l.C_root_litter_g, l.N_harvested_g,
                     l.C_leaf_litter_g, l.N_flush_g, l.Mn_flush_g}) {
        h.f(v);
    }
}

// 120일 실행 지문 (10일마다 스냅샷 + 끝)
inline std::uint64_t run() {
    auto created = sim::World::create(params(), scenario());
    if (!created) {
        return 0;
    }
    sim::World world = std::move(*created);
    Hasher h;
    const sim::TileArea all = sim::whole_grid(2, 3);
    (void)world.enqueue(sim::SetMulchCommand{.area = all, .mulch_id = "black_film"});
    (void)world.enqueue(sim::AddFertilizerCommand{.area = all, .fertilizer_id = "urea", .g_per_m2 = 20.0});
    for (std::uint32_t t = 0; t < kTicks; ++t) {
        if (t % 24 == 8) {
            (void)world.enqueue(sim::IrrigateCommand{.area = all, .amount_mm = 2.0});
        }
        world.step(1);
        if ((t + 1) % 240 == 0) {
            add_snapshot(h, world.snapshot());
        }
    }
    add_snapshot(h, world.snapshot());
    return h.value();
}

}  // namespace test::m5a_fingerprint
