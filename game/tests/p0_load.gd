# P0: 확장이 로드되고 SimWorld가 불리는지 확인 (헤드리스).
#   godot --headless --path game --script res://tests/p0_load.gd
extends SceneTree


func _init() -> void:
	var info := Engine.get_version_info()
	print("engine=%s" % info["string"])
	if not ClassDB.class_exists("SimWorld"):
		printerr("SimWorld class not registered")
		quit(1)
		return
	print("hello=%s" % ClassDB.class_call_static("SimWorld", "hello"))
	var w: Object = ClassDB.instantiate("SimWorld")
	print("instance=%s fp=%s" % [w.get_class(), ClassDB.class_call_static("SimWorld", "fp_state")])
	quit(0)
