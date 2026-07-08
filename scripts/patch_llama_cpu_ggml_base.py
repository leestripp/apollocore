#!/usr/bin/env python3
"""
patch_llama_cpu_ggml_base.py — fix GGML_USE_CPU definition for ggml-base.

Root cause
----------
llama.cpp's ggml/src/CMakeLists.txt sets GGML_USE_CPU on the 'ggml'
target (via ggml_add_backend() → target_compile_definitions(ggml PUBLIC
GGML_USE_CPU)), but ggml-backend-reg.cpp lives in the 'ggml-base' library.
Since GGML_USE_CPU is never propagated to ggml-base, the #ifdef
GGML_USE_CPU guard in ggml-backend-reg.cpp evaluates to false and
ggml_backend_cpu_reg() is never registered.

Effect: ggml_backend_reg_count() == 0 always, so
llama_model_load_from_file() returns nullptr with:
  "no backends are loaded. hint: use ggml_backend_load()..."

Fix: after the GGML_BACKEND_DL block in ggml/src/CMakeLists.txt, add
a GGML_CPU block that propagates GGML_USE_CPU to ggml-base.

Idempotent: running N times produces the same output as once.
"""
import pathlib
import re
import sys


def patch(path: pathlib.Path) -> int:
    text = path.read_text()

    # Check if already patched
    if "GGML_USE_CPU" in text and "ggml-base PUBLIC GGML_USE_CPU" in text:
        print(f"[cpu-patch] already applied — GGML_USE_CPU on ggml-base in {path}")
        return 0

    # Strip any previous patch block
    patch_block_re = re.compile(
        r"\n"
        r"# Patch: propagate GGML_USE_CPU to ggml-base so backend-reg.cpp\n"
        r"# can #ifdef GGML_USE_CPU and register the CPU backend.\n"
        r"if \(GGML_CPU\)\n"
        r"    target_compile_definitions\(ggml-base PUBLIC GGML_USE_CPU\)\n"
        r"endif\(\)\n*",
        re.MULTILINE,
    )
    text = patch_block_re.sub("", text)

    # Insert after the GGML_BACKEND_DL block:
    #   if (GGML_BACKEND_DL)
    #       target_compile_definitions(ggml-base PUBLIC GGML_BACKEND_DL)
    #   endif()
    insert_after = (
        "if (GGML_BACKEND_DL)\n"
        "    target_compile_definitions(ggml-base PUBLIC GGML_BACKEND_DL)\n"
        "endif()\n"
    )
    insert_block = (
        "if (GGML_BACKEND_DL)\n"
        "    target_compile_definitions(ggml-base PUBLIC GGML_BACKEND_DL)\n"
        "endif()\n"
        "\n"
        "# Patch: propagate GGML_USE_CPU to ggml-base so backend-reg.cpp\n"
        "# can #ifdef GGML_USE_CPU and register the CPU backend.\n"
        "if (GGML_CPU)\n"
        "    target_compile_definitions(ggml-base PUBLIC GGML_USE_CPU)\n"
        "endif()\n"
    )

    if insert_after not in text:
        print(f"error: anchor block not found in {path}", file=sys.stderr)
        return 1

    text = text.replace(insert_after, insert_block, 1)

    path.write_text(text)
    print(f"[cpu-patch] OK — GGML_USE_CPU propagated to ggml-base in {path}")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("usage: patch_llama_cpu_ggml_base.py <path-to-ggml-src-CMakeLists.txt>",
              file=sys.stderr)
        sys.exit(2)
    sys.exit(patch(pathlib.Path(sys.argv[1])))
