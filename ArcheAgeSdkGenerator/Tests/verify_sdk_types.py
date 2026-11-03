#!/usr/bin/env python3
"""Check generated SDK declarations and the complete reflected type graph."""

import argparse
import json
import re
from pathlib import Path


def verify(root: Path) -> None:
	metadata = json.loads((root / "IDA/unreal.json").read_text(encoding="utf-8"))
	known = {item["cppName"] for item in metadata["types"] + metadata["enums"]}
	known.update({"void", "bool", "char", "unsigned char", "int", "float", "double",
		"int8_t", "int16_t", "int32_t", "int64_t", "uint8_t", "uint16_t",
		"uint32_t", "uint64_t", "wchar_t", "uintptr_t", "size_t"})
	errors = []
	headers = sorted((root / "SDK").rglob("*.h")) + [root / "IDA/unreal.h"]
	basic = (root / "SDK/SDK/BasicType.h").read_text(encoding="utf-8")
	# Every captured reflected definition must survive generation. Checking
	# only the union of type names would miss globally deduplicated classes.
	emitted_paths = set()
	for path in (root / "SDK/SDK").glob("*.h"):
		emitted_paths.update(re.findall(r"^/// (?:Class|Struct|Enum) (.+)\r?$",
			path.read_text(encoding="utf-8"), re.MULTILINE))
	for item in metadata["types"] + metadata["enums"]:
		if item["package"] != "BasicType" and item["fullName"] not in emitted_paths:
			errors.append(f'{item["fullName"]}: captured definition was omitted from SDK headers')
	known.update(re.findall(r"^class (\w+);$", basic, re.MULTILINE))
	for path in headers:
		for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
			if "SDK_UNDEFINED" in line or "__um(" in line or "//TODO: Define " in line:
				errors.append(f"{path.name}:{number}: {line.strip()}")

	def check_type(field_type, context):
		name = field_type["name"].rstrip("* ")
		if field_type["propertyType"] == 0:
			errors.append(f"{context}: unknown property decoder for {name}")
		# Non-type template arguments encode captured optional storage sizes.
		if name not in known and not name.isdecimal():
			errors.append(f"{context}: missing type {name}")
		for child in field_type["subTypes"]:
			check_type(child, context)

	fields = 0
	for item in metadata["types"]:
		if item["package"] == "BasicType":
			continue  # Custom templates contain symbolic parameters, e.g. T.
		for field in item["fields"]:
			if not field["generatedPadding"]:
				check_type(field["type"], f'{item["cppName"]}.{field["name"]}')
			fields += 1
	for function in metadata["functions"]:
		check_type(function["returnType"], function["fullName"])
		for parameter in function["parameters"]:
			check_type(parameter["type"], function["fullName"])

	# Game-specific smoke checks must refer to ArcheAge, not the Aion2 SDK
	# from which this generator was originally forked.
	assert "AX5PlayerController" in known, "ArcheAge player controller is missing"
	assert {"UObject", "AActor", "APawn", "FVector", "UWorld"} <= known, (
		"Core engine types are missing")
	engine = (root / "SDK/SDK/Engine.h").read_text(encoding="utf-8")
	assert "TFieldPath<FField>& Value" in engine, (
		"The field-path function decoder is incorrect")
	all_fields = {f'{item["cppName"]}.{field["name"]}': field
		for item in metadata["types"] for field in item["fields"]}
	predicts = all_fields["UVerseClass.PredictsVarNames"]["type"]
	assert predicts["subTypes"][0]["name"] == "FAnsiString", "ANSI map key decoder is missing"
	alias = next(function for function in metadata["functions"]
		if function["fullName"] == "/Script/X5.X5FriendWidget.OnFriendAliasModifyDialogConfirm")
	assert alias["parameters"][0]["type"]["name"] == "TOptional", "Optional parameter decoder is missing"
	assert alias["parameters"][0]["type"]["subTypes"][0]["name"] == "FText"
	assert any(field["name"] == "CachedHasVertexColors" and field["type"]["name"] == "TOptional"
		for item in metadata["types"] for field in item["fields"]), "Optional member was discarded"
	assert not re.search(r"^class F(?:null)?;$", basic, re.MULTILINE)
	assert not errors, "\n".join(errors[:100]) + f"\n{len(errors)} unresolved references"
	print(f"PASS: {len(headers)} headers, {fields} fields, "
		f'{len(metadata["enums"])} enums, {len(metadata["functions"])} functions; '
		"zero unresolved types or member placeholders")


if __name__ == "__main__":
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("dump", type=Path)
	verify(parser.parse_args().dump)
