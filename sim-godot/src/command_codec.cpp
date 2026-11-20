#include "command_codec.hpp"

#include <format>
#include <string_view>
#include <type_traits>
#include <variant>

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_float64_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_int64_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/variant.hpp>

namespace simgodot {

namespace {

std::string to_utf8(const godot::String& s) {
    const godot::CharString cs = s.utf8();
    return std::string(cs.get_data(), static_cast<std::size_t>(cs.length()));
}

// 실수는 왕복되는 최단 표기. TOML 정수로 읽히지 않게 소수점이 없으면 ".0"을 붙여요 (inf·nan은 그대로: 로더가 거부).
std::string format_double(double d) {
    std::string s = std::format("{}", d);
    if (s.find_first_of(".eEn") == std::string::npos) {
        s += ".0";
    }
    return s;
}

bool is_bare_key(std::string_view key) {
    if (key.empty()) {
        return false;
    }
    for (const char c : key) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
                        c == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

std::expected<std::string, std::string> format_value(const godot::Variant& v, std::string_view key) {
    switch (v.get_type()) {
        case godot::Variant::BOOL:
            return std::string(static_cast<bool>(v) ? "true" : "false");
        case godot::Variant::INT:
            return std::format("{}", static_cast<int64_t>(v));
        case godot::Variant::FLOAT:
            return format_double(static_cast<double>(v));
        case godot::Variant::STRING:
        case godot::Variant::STRING_NAME:
            return toml_quote(to_utf8(static_cast<godot::String>(v)));
        case godot::Variant::ARRAY: {
            const godot::Array a = v;
            std::string out = "[";
            for (int64_t i = 0; i < a.size(); ++i) {
                const godot::Variant& e = a[i];
                if (e.get_type() == godot::Variant::ARRAY || e.get_type() == godot::Variant::DICTIONARY) {
                    return std::unexpected(std::format("key '{}' must not contain nested arrays", key));
                }
                auto s = format_value(e, key);
                if (!s) {
                    return s;
                }
                out += (i == 0 ? "" : ", ") + *s;
            }
            return out + "]";
        }
        case godot::Variant::PACKED_INT32_ARRAY: {
            const godot::PackedInt32Array a = v;
            std::string out = "[";
            for (int64_t i = 0; i < a.size(); ++i) {
                out += std::format("{}{}", i == 0 ? "" : ", ", a[i]);
            }
            return out + "]";
        }
        case godot::Variant::PACKED_INT64_ARRAY: {
            const godot::PackedInt64Array a = v;
            std::string out = "[";
            for (int64_t i = 0; i < a.size(); ++i) {
                out += std::format("{}{}", i == 0 ? "" : ", ", a[i]);
            }
            return out + "]";
        }
        case godot::Variant::PACKED_FLOAT64_ARRAY: {
            const godot::PackedFloat64Array a = v;
            std::string out = "[";
            for (int64_t i = 0; i < a.size(); ++i) {
                out += (i == 0 ? "" : ", ") + format_double(a[i]);
            }
            return out + "]";
        }
        default:
            return std::unexpected(std::format("key '{}' has unsupported value type {}", key,
                                               to_utf8(godot::Variant::get_type_name(v.get_type()))));
    }
}

godot::Array area_array(const sim::TileArea& a) {
    godot::Array arr;
    arr.push_back(a.row);
    arr.push_back(a.col);
    arr.push_back(a.rows);
    arr.push_back(a.cols);
    return arr;
}

godot::String gstr(std::string_view s) {
    return godot::String::utf8(s.data(), static_cast<int64_t>(s.size()));
}

}  // namespace

std::string toml_quote(std::string_view s) {
    std::string out = "\"";
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        if (c == '"') {
            out += "\\\"";
        } else if (c == '\\') {
            out += "\\\\";
        } else if (c < 0x20 || c == 0x7f) {
            out += std::format("\\u{:04X}", static_cast<unsigned>(c));
        } else {
            out += ch;
        }
    }
    return out + "\"";
}

std::expected<std::string, std::string> command_dict_to_toml(const godot::Dictionary& command, std::uint64_t tick) {
    std::string out = std::format("[[schedule]]\ntick = {}\n", tick);
    const godot::Array keys = command.keys();
    for (int64_t i = 0; i < keys.size(); ++i) {
        const godot::Variant& k = keys[i];
        if (k.get_type() != godot::Variant::STRING && k.get_type() != godot::Variant::STRING_NAME) {
            return std::unexpected(std::string("command keys must be strings"));
        }
        const std::string key = to_utf8(static_cast<godot::String>(k));
        if (key == "tick" || key == "repeat_every_ticks" || key == "repeat_count") {
            return std::unexpected(std::format("key '{}' is not allowed in enqueue (the wrapper sets the tick)", key));
        }
        if (!is_bare_key(key)) {
            return std::unexpected(std::format("unknown key '{}'", key));
        }
        auto value = format_value(command[k], key);
        if (!value) {
            return std::unexpected(std::move(value.error()));
        }
        out += key + " = " + *value + "\n";
    }
    return out;
}

godot::Dictionary command_to_dict(const sim::Command& command) {
    godot::Dictionary d;
    std::visit(
        [&](const auto& c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, sim::NoopCommand>) {
                d["action"] = "noop";
            } else if constexpr (std::is_same_v<T, sim::IrrigateCommand>) {
                d["action"] = "irrigate";
                d["area"] = area_array(c.area);
                d["amount_mm"] = c.amount_mm;
            } else if constexpr (std::is_same_v<T, sim::AddCompostCommand>) {
                d["action"] = "add_compost";
                d["area"] = area_array(c.area);
                d["compost"] = gstr(c.compost_id);
                d["kg_per_m2"] = c.kg_per_m2;
            } else if constexpr (std::is_same_v<T, sim::AddFertilizerCommand>) {
                d["action"] = "add_fertilizer";
                d["area"] = area_array(c.area);
                d["fertilizer"] = gstr(c.fertilizer_id);
                d["g_per_m2"] = c.g_per_m2;
            } else if constexpr (std::is_same_v<T, sim::AddLimeCommand>) {
                d["action"] = "add_lime";
                d["area"] = area_array(c.area);
                d["lime"] = gstr(c.lime_id);
                d["g_per_m2"] = c.g_per_m2;
            } else if constexpr (std::is_same_v<T, sim::TillCommand>) {
                d["action"] = "till";
                d["area"] = area_array(c.area);
            } else if constexpr (std::is_same_v<T, sim::SetWindowCommand>) {
                d["action"] = "set_window";
                d["layer"] = c.layer;
                d["side"] = c.side == sim::WindowSide::Left ? "left" : "right";
                d["open_fraction"] = c.open_fraction;
            } else if constexpr (std::is_same_v<T, sim::SetHeaterCommand>) {
                d["action"] = "set_heater";
                d["on"] = c.on;
                d["setpoint_degC"] = c.setpoint_degC;
            } else if constexpr (std::is_same_v<T, sim::SetCoverLayersCommand>) {
                d["action"] = "set_cover_layers";
                d["layers"] = c.layers;
            } else if constexpr (std::is_same_v<T, sim::ThinFlowersCommand>) {
                d["action"] = "thin_flowers";
                d["area"] = area_array(c.area);
                d["per_plant"] = c.per_plant;
            } else if constexpr (std::is_same_v<T, sim::SetMulchCommand>) {
                d["action"] = "set_mulch";
                d["area"] = area_array(c.area);
                d["mulch"] = gstr(c.mulch_id);
            } else if constexpr (std::is_same_v<T, sim::ApplyDisinfectantCommand>) {
                d["action"] = "apply_disinfectant";
                d["area"] = area_array(c.area);
                d["disinfectant"] = gstr(c.disinfectant_id);
                d["g_per_m2"] = c.g_per_m2;
            } else if constexpr (std::is_same_v<T, sim::AddInoculantCommand>) {
                d["action"] = "add_inoculant";
                d["area"] = area_array(c.area);
                d["inoculant"] = gstr(c.inoculant_id);
                d["g_per_m2"] = c.g_per_m2;
            } else if constexpr (std::is_same_v<T, sim::PlantCommand>) {
                d["action"] = "plant";
                d["seedling_grade"] = gstr(c.seedling_grade);
                d["plants_per_m2"] = c.plants_per_m2;
            } else if constexpr (std::is_same_v<T, sim::SetFanCommand>) {
                d["action"] = "set_fan";
                d["on"] = c.on;
            } else if constexpr (std::is_same_v<T, sim::RemovePlantCommand>) {
                d["action"] = "remove_plant";
                d["area"] = area_array(c.area);
            } else if constexpr (std::is_same_v<T, sim::ReleasePredatorsCommand>) {
                d["action"] = "release_predators";
                d["area"] = area_array(c.area);
                d["predator"] = gstr(c.predator_id);
                d["per_m2"] = c.per_m2;
            } else if constexpr (std::is_same_v<T, sim::InstallDripCommand>) {
                d["action"] = "install_drip";
            } else {
                static_assert(sizeof(T) == 0, "unhandled sim::Command alternative");
            }
        },
        command);
    return d;
}

std::string format_schedule_entries(const std::vector<simdata::ScheduledCommand>& entries,
                                    const std::vector<std::string>& comments) {
    std::string out;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (i < comments.size() && !comments[i].empty()) {
            out += "# " + comments[i] + "\n";
        }
        if (std::holds_alternative<sim::NoopCommand>(entries[i].command)) {
            out += std::format("# tick {}: noop (not representable)\n\n", entries[i].tick);
            continue;
        }
        auto text = command_dict_to_toml(command_to_dict(entries[i].command), entries[i].tick);
        out += text ? *text : std::format("# tick {}: cannot format: {}\n", entries[i].tick, text.error());
        out += "\n";
    }
    return out;
}

}  // namespace simgodot
