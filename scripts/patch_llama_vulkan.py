#!/usr/bin/env python3
"""
patch_llama_vulkan.py — idempotently enable the Vulkan backend in
the ggml/CMakeLists.txt that FetchContent pulls into
build/_deps/llama_cpp-src/.

Background
----------
llama.cpp's ggml/CMakeLists.txt declares a `GGML_VULKAN` option but
never actually uses it — the Vulkan backend is never built by
default. Our project enables `-DGGML_VULKAN=ON`, which sets the cache
var, but nothing calls `ggml_add_backend(VULKAN)`. So we patch in
such a call.

The complication: ggml's CMakeLists.txt also has a trailing
`ggml_add_backend(Vulkan)` near the bottom (a "build all backends"
list). If we just append our call, CMake errors with
"binary directory already used" because both calls end up
invoking `add_subdirectory(ggml-vulkan)`.

So this script:
  1. Strips every existing Vulkan / VULKAN activation (regardless of
     case, indentation, or whether it's inside our if/end wrapper).
  2. Inserts exactly one activation block after
     `ggml_add_backend(CPU)`.

The result is a file with exactly one Vulkan backend activation,
regardless of how many times this script has run.

Idempotent: yes. Running this script N times produces the same
output as running it once.
"""

import pathlib
import re
import sys


def patch(path: pathlib.Path) -> int:
    text = path.read_text()

    # Strip every Vulkan / VULKAN activation block. The patch we add
    # later matches this same regex so we can run the strip and the
    # insert in either order.
    #
    # Patterns handled:
    #   (a) bare line:    ggml_add_backend(Vulkan)
    #   (b) bare UPPER:   ggml_add_backend(VULKAN)
    #   (c) indented:        ggml_add_backend(VULKAN)
    #   (d) wrapped patch block (# Patch: ... endif() ...)
    # We delete all four.
    vulkan_line_re = re.compile(
        r"^[ \t]*ggml_add_backend\([Vv]ulkan\)\s*$",
        re.MULTILINE,
    )
    patch_block_re = re.compile(
        r"# Patch: enable Vulkan backend \([^)]*\)\n"
        r"if \(GGML_VULKAN\)\n"
        r"    ggml_add_backend\(VULKAN\)\n"
        r"endif\(\)\n*",
        re.MULTILINE,
    )
    text = vulkan_line_re.sub("", text)
    text = patch_block_re.sub("", text)

    # Insert exactly one activation block after ggml_add_backend(CPU).
    # Use the FIRST occurrence (re.sub with count=1). Anchoring on the
    # unique "ggml_add_backend(CPU)" line makes this idempotent: even
    # if the file had a stray CPU line elsewhere, only the canonical
    # first one gets the patch.
    patch_text = (
        "\n"
        "# Patch: enable Vulkan backend (GGML_VULKAN option was declared but never used)\n"
        "if (GGML_VULKAN)\n"
        "    ggml_add_backend(VULKAN)\n"
        "endif()\n"
    )
    text, n = re.subn(
        r"(ggml_add_backend\(CPU\)\n)",
        r"\1" + patch_text,
        text,
        count=1,
    )
    if n != 1:
        print(f"error: did not find ggml_add_backend(CPU) in {path}", file=sys.stderr)
        return 1

    path.write_text(text)
    # Count both case forms so the printed number is accurate.
    final_count = len(re.findall(r"ggml_add_backend\([Vv]?[Uu]?[Ll]?[Kk]?[Aa]?[Nn]\)",
                                  text))
    print(f"[vulkan-patch] OK — {final_count} Vulkan activation(s) in {path}")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("usage: patch_llama_vulkan.py <path-to-ggml-CMakeLists.txt>",
              file=sys.stderr)
        sys.exit(2)
    sys.exit(patch(pathlib.Path(sys.argv[1])))