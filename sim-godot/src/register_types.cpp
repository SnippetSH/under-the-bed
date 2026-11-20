// sim-godot 확장 진입점 (ADR-0015). 클래스는 SimWorld 하나만 등록해요 (m6-overview 4장).
#include <cstdio>

#include <gdextension_interface.h>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "fp_state.hpp"
#include "sim_world.hpp"

namespace {

void initialize_sim_godot(godot::ModuleInitializationLevel level) {
    if (level != godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
    // R2/U5: 확장이 올라올 때의 부동소수점 제어 상태를 남겨요 (프로토타입 진단용)
    const simgodot::FpState s = simgodot::read_fp_state();
    char line[128];
    std::snprintf(line, sizeof(line), "fp_state process=godot at=load mxcsr=0x%08x controlfp=0x%08x fma3=%d", s.mxcsr,
                  s.controlfp, s.fma3_enabled);
    godot::UtilityFunctions::print(line);
    GDREGISTER_CLASS(simgodot::SimWorld);
}

void uninitialize_sim_godot(godot::ModuleInitializationLevel level) {
    (void)level;
}

}  // namespace

extern "C" {

GDExtensionBool GDE_EXPORT sim_godot_library_init(GDExtensionInterfaceGetProcAddress get_proc_address,
                                                  GDExtensionClassLibraryPtr library,
                                                  GDExtensionInitialization* initialization) {
    godot::GDExtensionBinding::InitObject init_obj(get_proc_address, library, initialization);
    init_obj.register_initializer(initialize_sim_godot);
    init_obj.register_terminator(uninitialize_sim_godot);
    init_obj.set_minimum_library_initialization_level(godot::MODULE_INITIALIZATION_LEVEL_SCENE);
    return init_obj.init();
}

}
