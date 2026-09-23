from __future__ import annotations

import re


def _matching_brace(source: str, opening: int) -> int:
    depth = 0
    state = "code"
    escaped = False
    i = opening
    while i < len(source):
        char = source[i]
        next_char = source[i + 1] if i + 1 < len(source) else ""

        if state == "line_comment":
            if char == "\n":
                state = "code"
        elif state == "block_comment":
            if char == "*" and next_char == "/":
                state = "code"
                i += 1
        elif state in ("string", "char"):
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif (state == "string" and char == '"') or (state == "char" and char == "'"):
                state = "code"
        elif char == "/" and next_char == "/":
            state = "line_comment"
            i += 1
        elif char == "/" and next_char == "*":
            state = "block_comment"
            i += 1
        elif char == '"':
            state = "string"
        elif char == "'":
            state = "char"
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return i
        i += 1
    raise ValueError("unbalanced C++ braces")


def extract_braced_body(source: str, marker: str) -> str:
    marker_index = source.find(marker)
    if marker_index < 0:
        raise ValueError(f"C++ marker not found: {marker}")
    opening = source.find("{", marker_index + len(marker))
    if opening < 0:
        raise ValueError(f"opening brace not found after: {marker}")
    closing = _matching_brace(source, opening)
    return source[opening + 1:closing]


def normalize_cpp(source: str) -> str:
    source = re.sub(r"//[^\n]*", " ", source)
    source = re.sub(r"/\*.*?\*/", " ", source, flags=re.DOTALL)
    return re.sub(r"\s+", " ", source).strip()
