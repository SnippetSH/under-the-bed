// sim-godot 확장 진입점 (ADR-0015). 클래스는 SimWorld 하나만 등록해요 (m6-overview 4장).
// 진입 기호 이름은 game/under_the_bed.gdextension의 entry_symbol과 같아야 해요.
#include <gdextension_interface.h>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

#include "sim_world.hpp"

namespace {

void initialize_sim_godot(godot::ModuleInitializationLevel level) {
    if (level != godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
    GDREGISTER_CLASS(simgodot::SimWorld);
}

void uninitialize_sim_godot(godot::ModuleInitializationLevel level) {
    (void)level;  // 전역 상태가 없어서 정리할 것이 없어요
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
