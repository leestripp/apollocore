#!/usr/bin/env python3
"""
patch_llama_cpu_backend.py — idempotently patch llama.cpp's ggml/src/CMakeLists.txt
to propagate GGML_USE_CPU to ggml-base so ggml-backend-reg.cpp can #ifdef it.

Background
----------
llama.cpp's ggml/src/CMakeLists.txt sets GGML_CPU via option() but does NOT
propagate it to the ggml-base INTERFACE library. When ApolloCore compiles
ggml-backend-reg.cpp, it #ifdefs on GGML_USE_CPU but the symbol is not
visible to ggml-base's consumers — causing a "no backends are loaded" crash.

The fix: after ggml_add_backend(CPU), inject:
    if (GGML_CPU)
        target_compile_definitions(ggml-base PUBLIC GGML_USE_CPU)
    endif()

Idempotent: running N times = same result as running 1 time.
"""
import pathlib
import re
import sys

PATCH_BLOCK = """# ApolloCore patch: propagate GGML_USE_CPU to ggml-base
# (needed so ggml-backend-reg.cpp can #ifdef on GGML_USE_CPU)
if (GGML_CPU)
    target_compile_definitions(ggml-base PUBLIC GGML_USE_CPU)
endif()
"""


def patch(path: pathlib.Path) -> int:
    text = path.read_text()

    marker = "# ApolloCore patch: propagate GGML_USE_CPU to ggml-base"
    if marker in text:
        return 0  # already patched

    # Find "ggml_add_backend(CPU)" and insert patch after it.
    # Handle both "ggml_add_backend(CPU)" and "ggml_add_backend(cpu)".
    pattern = re.compile(r'^(\s*ggml_add_backend\(CPU\))',
                         re.MULTILINE | re.IGNORECASE)
    m = pattern.search(text)
    if not m:
        print(f"WARNING: ggml_add_backend(CPU) not found in {path}")
        return 1

    insert_pos = m.end()
    patched = text[:insert_pos] + "\n" + PATCH_BLOCK + text[insert_pos:]
    path.write_text(patched)
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <path-to-ggml-src-CMakeLists.txt>")
        sys.exit(1)
    path = pathlib.Path(sys.argv[1])
    if not path.exists():
        print(f"ERROR: {path} not found")
        sys.exit(1)
    sys.exit(patch(path))