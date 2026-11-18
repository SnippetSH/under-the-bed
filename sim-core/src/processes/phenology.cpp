// docs/spec/strawberry-phenology.md — 활착, 화아분화, 화방 발달·개화 (셀 단계 h1)
#include <algorithm>
#include <cstddef>

#include "sim/calendar.hpp"
#include "sim/processes.hpp"

namespace sim::process {

double update_phenology(Crop& crop, const Params& params, const CropEnvironment& env, double fruit_dry_g_per_m2,
                        Pcg32& biology_rng) {
    const PhenologyParams& ph = params.phenology;
    const double air_degC = env.house_air_temp_degC;

    // 1. 활착
    crop.establishment_index =
        std::min(1.0, crop.establishment_index + std::max(0.0, ph.establishment_rate_per_hour *
                                                                   ph.establishment_temp_curve(air_degC)));
    const double growth_factor_establish = ph.establishment_growth_curve(crop.establishment_index);

    // 2. 화아분화: 활착이 끝난 뒤에만 진행해요 ("활착 전에는 화아분화가 진행되지 않아요")
    if (crop.establishment_index >= 1.0) {
        const double day_length_h = params.calendar.day_length_curve(static_cast<double>(env.day_of_year));
        const double required_N = nutrient_requirement(crop, params.crop, fruit_dry_g_per_m2).N;
        const double n_ratio = required_N > 0.0 ? std::max(0.0, crop.plant_N_g_per_m2) / required_N : 1.0;
        crop.induction_index += std::max(0.0, ph.induction_rate_per_hour) *
                                ph.induction_day_length_curve(day_length_h) * ph.induction_temp_curve(air_degC) *
                                ph.induction_nitrogen_curve(n_ratio);
        while (crop.induction_index >= 1.0) {
            crop.induction_index -= 1.0;
            const double crown_g_per_plant =
                crop.plants_per_m2 > 0.0 ? std::max(0.0, crop.crown_g_per_m2) / crop.plants_per_m2 : 0.0;
            crop.trusses.push_back(Truss{
                .initiated_tick = env.tick,
                .development_index = 0.0,
                .flowers_per_m2 = ph.flowers_per_truss_curve(crown_g_per_plant) * crop.plants_per_m2,
                .opened = false,
            });
        }
    }

    // 3. 화방 발달과 개화 (분화 순서대로, 개화한 화방은 더 계산하지 않음)
    const double truss_increment = ph.truss_degree_days_to_anthesis > 0.0
                                       ? std::max(0.0, air_degC - ph.truss_base_temp_degC) / kHoursPerDay /
                                             ph.truss_degree_days_to_anthesis
                                       : 0.0;
    for (std::size_t i = 0; i < crop.trusses.size(); ++i) {
        Truss& truss = crop.trusses[i];
        if (truss.opened) {
            continue;
        }
        truss.development_index = std::min(1.0, truss.development_index + truss_increment);
        if (truss.development_index >= 1.0) {
            truss.opened = true;
            truss.opened_tick = env.tick;
            create_flowers(crop, static_cast<std::uint32_t>(i), truss.flowers_per_m2, params.fruit, env.tick,
                           biology_rng);
        }
    }
    return growth_factor_establish;
}

}  // namespace sim::process
