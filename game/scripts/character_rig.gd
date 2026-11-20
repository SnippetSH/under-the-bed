# 3D 캐릭터 자리 (M6b P8): game/art/models/character.glb가 있으면 그 모델(뼈대·애니메이션 포함)을 쓰고, 없으면 평면 그림을 써요.
# 앞면 = +Z, 발바닥 = 원점, 1 단위 = 1 m. 애니메이션 이름: idle, walk, run, work (없는 것은 아래 순서로 대신해요).
#   run  → run, 없으면 walk를 빠르게(run_speed_factor배)  → idle → 멈춤
#   walk → walk → idle → 멈춤
#   work → work → idle → 멈춤
#   idle → idle → 멈춤
extends Node3D

const MODEL_PATH := "res://art/models/character.glb"
# 화면이 찾는 자리. 검사(C13)가 "파일이 없을 때"를 보려고 없는 경로로 바꿔요
static var model_path := MODEL_PATH

var model: Node3D = null
var anim: AnimationPlayer = null
var current_clip := ""
var current_state := ""


static func model_exists() -> bool:
	return FileAccess.file_exists(model_path)


# 모델을 읽어 붙여요(path가 비면 model_path). 실패하면 false (평면 그림으로 돌아가요)
func load_model(path := "") -> bool:
	if path == "":
		path = model_path
	var doc := GLTFDocument.new()
	var st := GLTFState.new()
	if doc.append_from_file(ProjectSettings.globalize_path(path), st) != OK:
		push_warning("캐릭터 모델을 읽지 못했어요: %s" % path)
		return false
	var root := doc.generate_scene(st)
	if root == null:
		return false
	model = root
	add_child(root)
	anim = _find_player(root)
	if anim != null:
		for n in ["idle", "walk", "run"]:
			if anim.has_animation(n):
				anim.get_animation(n).loop_mode = Animation.LOOP_LINEAR
	return true


static func _find_player(n: Node) -> AnimationPlayer:
	if n is AnimationPlayer:
		return n
	for c in n.get_children():
		var p := _find_player(c)
		if p != null:
			return p
	return null


func clip_names() -> PackedStringArray:
	return anim.get_animation_list() if anim != null else PackedStringArray()


# 바라보는 방향 (게임 좌표 x, z) → 모델의 +Z가 그쪽을 보게
func set_facing(f: Vector2i) -> void:
	if f != Vector2i.ZERO:
		rotation.y = atan2(float(f.x), float(f.y))


# state: idle / walk / run / work. 실제로 튼 클립 이름을 돌려줘요 ("" = 클립 없음, 멈춤)
func play_state(state: String, run_factor := 1.6) -> String:
	current_state = state
	if anim == null:
		return ""
	var chain: Array = {"run": ["run", "walk", "idle"], "walk": ["walk", "idle"], "work": ["work", "idle"]}.get(state, ["idle"])
	var clip := ""
	for c in chain:
		if anim.has_animation(c):
			clip = c
			break
	var speed := run_factor if (state == "run" and clip == "walk") else 1.0
	if clip == "":
		if anim.is_playing():
			anim.stop()
		current_clip = ""
		return ""
	if clip != current_clip or not anim.is_playing():
		anim.play(clip)
		current_clip = clip
	anim.speed_scale = speed
	return clip
