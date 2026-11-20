# 시작 화면 -> 본 화면으로 넘기는 선택값 (정적 변수)
extends RefCounted

static var scenario_id := ""   # 새 판으로 시작할 시나리오 id (game_config.cfg [start] scenario_order)
static var load_id := ""       # 비어 있지 않으면 이 세이브 칸을 불러와요 (SaveStore 칸 id)


static func choose_scenario(id: String) -> void:
	scenario_id = id
	load_id = ""


static func choose_save(id: String) -> void:
	load_id = id
