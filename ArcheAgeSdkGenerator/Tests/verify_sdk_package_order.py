#!/usr/bin/env python3
"""Check that the generated SDK headers are written in a usable order.

The generated headers do not include each other, so a type has to be written
before it is used.  A base class and a by value member need the complete type,
a function signature and a template argument only need a declaration.

The audit walks the include order of SDK.h and reports every type that is used
before it is written down.
"""

import argparse
import re
from pathlib import Path

DEF_RE = re.compile(r'^(?:class|struct)\s+([A-Za-z_][A-Za-z0-9_]*)')
ENUM_RE = re.compile(r'^enum class\s+([A-Za-z_][A-Za-z0-9_]*)')
INHERIT_RE = re.compile(r'^(?:class|struct)\s+([A-Za-z_][A-Za-z0-9_]*)\s*:\s*(?:public\s+)?([A-Za-z_][A-Za-z0-9_]*)')
MEMBER_RE = re.compile(r'^\t(.+?)\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?:\[[^\]]*\])?\s*;\s*//\s*0x')
FUNC_RE = re.compile(r'^\t(?:static\s+)?(?:virtual\s+)?(.+?)\s+([A-Za-z_][A-Za-z0-9_]*)\s*\((.*)\)\s*(?:const)?\s*;')
INCLUDE_RE = re.compile(r'^#include "SDK/([^"]+)\.h"')
IDENTIFIER_RE = re.compile(r'^[A-Za-z_][A-Za-z0-9_]*$')

# containers that only keep a pointer or an index of the element
POINTER_CONTAINERS = {
    "TArray", "TWeakObjectPtr", "TSoftObjectPtr", "TLazyObjectPtr",
    "TScriptInterface", "TFieldPath", "TEnumAsByte", "TObjectPtr", "TSubclassOf",
}
# containers that never mention the element type
OPAQUE_CONTAINERS = {"TSet", "TOptional", "FDelegate", "FMulticastDelegate"}
# containers that store the element by value
VALUE_CONTAINERS = {"TMap", "TPair", "TSetElement", "TPersistentObjectPtr"}

# the hand written template header, it defines the containers themselves
TEMPLATE_HEADER = "BasicType"


def split_template(text):
    """Split "TMap<Foo, Bar>" into ("TMap", ["Foo", "Bar"])."""
    start = text.find("<")
    if start < 0:
        return text.strip(), []
    depth = 0
    parts = []
    current = ""
    for ch in text[start:]:
        if ch == "<":
            depth += 1
            if depth == 1:
                continue
        elif ch == ">":
            depth -= 1
            if depth == 0:
                parts.append(current)
                current = ""
                continue
        if depth >= 1:
            current += ch
    return text[:start].strip(), [part.strip() for part in parts if part.strip()]


def clean(name):
    name = name.strip()
    name = re.sub(r"^(class|struct)\s+", "", name)
    name = re.sub(r"\bconst\b", " ", name).strip()
    name = name.rstrip("*&").strip()
    name = name.split("[")[0].strip()
    return re.sub(r"::.*$", "", name).strip()


def scan_header(path):
    """Returns the definitions and the uses of one generated header."""
    definitions = []
    uses = []
    in_functions = False
    for number, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
        match = INHERIT_RE.match(line)
        if match:
            definitions.append((match.group(1), number))
            uses.append((match.group(2), number, "base class"))
            in_functions = False
            continue
        match = DEF_RE.match(line)
        if match:
            definitions.append((match.group(1), number))
            in_functions = False
            continue
        match = ENUM_RE.match(line)
        if match:
            definitions.append((match.group(1), number))
            continue
        if "/// Functions" in line:
            in_functions = True
            continue
        if in_functions:
            match = FUNC_RE.match(line)
            if match:
                uses.append((match.group(1), number, "return type"))
                for part in match.group(3).split(","):
                    part = part.strip()
                    if not part:
                        continue
                    pieces = part.rsplit(None, 1)
                    if len(pieces) == 2:
                        uses.append((pieces[0], number, "parameter"))
            continue
        match = MEMBER_RE.match(line)
        if match:
            uses.append((match.group(1), number, "member"))
    return definitions, uses


def required_names(expression, by_value):
    """Returns the type names that have to be known before the expression."""
    expression = expression.strip()
    if not expression:
        return []
    # "class X*" / "struct X*" declares the type itself
    if re.match(r"^(class|struct)\s", expression):
        return []
    outer, arguments = split_template(expression)
    outer = clean(outer)
    if not arguments:
        if not IDENTIFIER_RE.match(outer):
            return []  # a builtin like "unsigned char"
        return [outer]
    if outer in OPAQUE_CONTAINERS:
        return []
    element_by_value = outer in VALUE_CONTAINERS
    names = []
    for argument in arguments:
        names.extend(required_names(argument, element_by_value))
    return names


def verify(root: Path, verbose: bool) -> int:
    sdk = root / "SDK"
    master = sdk / "SDK.h"
    if not master.is_file():
        print(f"missing {master}")
        return 1

    include_order = []
    for line in master.read_text(encoding="utf-8").splitlines():
        match = INCLUDE_RE.match(line)
        if match:
            include_order.append(match.group(1))

    headers = []
    for position, package in enumerate(include_order):
        header = sdk / "SDK" / f"{package}.h"
        if not header.is_file():
            continue
        definitions, uses = scan_header(header)
        headers.append((position, package, header, definitions, uses))

    # every type and the place where it is written down
    written = {}
    written_in_package = {}
    for position, package, header, definitions, _uses in headers:
        local = written_in_package.setdefault(package, {})
        for name, number in definitions:
            local.setdefault(name, number)
            written.setdefault(name, (position, package, number))

    errors = []
    for position, package, header, _definitions, uses in headers:
        if package == TEMPLATE_HEADER:
            continue  # the container templates themselves
        local = written_in_package.get(package, {})
        for expression, number, context in uses:
            by_value = context in ("base class", "member")
            for name in required_names(expression, by_value):
                if name in local:
                    # the type is written in this header, it has to come first
                    if local[name] > number:
                        errors.append(
                            f"{package}.h:{number}: {context} uses {name} before it is written")
                    continue
                target = written.get(name)
                if target is None:
                    continue  # not a reflected type, the type audit covers it
                if target[0] < position:
                    continue  # written in an earlier header
                errors.append(f"{package}.h:{number}: {context} uses {name} before it is written")

    # every package has to be included exactly once
    seen = set()
    for name in include_order:
        if name in seen:
            errors.append(f"{name}.h is included more than once")
        seen.add(name)

    if errors:
        print(f"FAIL: {len(errors)} ordering problems in the generated SDK")
        for error in errors[:40]:
            print(f"  {error}")
        if len(errors) > 40:
            print(f"  ... and {len(errors) - 40} more")
        return 1

    if verbose:
        print(f"PASS: {len(include_order)} package headers are written in a usable order")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dump", help="path of the generated dump")
    parser.add_argument("--verbose", action="store_true")
    arguments = parser.parse_args()
    return verify(Path(arguments.dump), arguments.verbose)


if __name__ == "__main__":
    raise SystemExit(main())
