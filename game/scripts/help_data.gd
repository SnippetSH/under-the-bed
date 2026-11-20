# 도움말 글 읽기 (M6b P11). 글은 config/help_ko.json에 있어서 스크립트를 고치지 않고 다듬을 수 있어요.
# 조작법 쪽과 화면 아래 키 안내 줄은 같은 keys 표에서 만들어요.
extends RefCounted

const PATH := "res://config/help_ko.json"
static var _data: Dictionary = {}


static func data() -> Dictionary:
	if _data.is_empty():
		var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(PATH))
		if typeof(parsed) != TYPE_DICTIONARY:
			push_error("도움말 파일을 읽지 못했어요: %s" % PATH)
			return {"topics": [], "keys": []}
		_data = parsed
	return _data


# [{id, title, body}] 마지막에 조작법
static func pages() -> Array:
	var out := []
	for t in data().get("topics", []):
		# P15: 리드 한 줄, 그다음 "•" 항목들 (한 줄 = 한 문단)
		var items := PackedStringArray()
		for pt in t.get("points", []):
			items.push_back("•  " + str(pt))
		out.append({"id": t["id"], "title": t["title"], "body": str(t.get("lead", "")) + "\n" + "\n".join(items)})   # 문단 간격은 화면(RichTextLabel)이 정해요
	out.append({"id": "controls", "title": str(data().get("controls_title", "조작법")), "body": controls_text()})
	return out


static func controls_text() -> String:
	var lines := PackedStringArray([str(data().get("controls_intro", ""))])
	for k in data().get("keys", []):
		lines.push_back("• " + str(k["long"]))
	return "\n".join(lines)


# 화면 아래 한 줄 키 안내 ("WASD 걷기 · Shift 달리기 · …")
static func key_hint() -> String:
	var parts := PackedStringArray()
	for k in data().get("keys", []):
		parts.push_back((str(k["key"]) + " " + str(k["short"])).strip_edges())
	return " · ".join(parts)
