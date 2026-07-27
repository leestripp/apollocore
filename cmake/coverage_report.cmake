# coverage_report.cmake — per-file gcov line-coverage summary for project
# sources. Invoked by the `coverage` target with -DSOURCE_DIR/-DBUILD_DIR.
#
# Walks every .gcda under the build tree, asks gcov for its no-output
# summary (`gcov -n`), and keeps only the lines for our own sources
# (src/, include/, tests/ — never _deps). Headers and shared MCP sources
# are compiled into several test binaries, so the same file appears many
# times; we keep the entry from the compilation unit that measured the
# most lines. Pure gcov; no lcov/gcovr.

file(GLOB_RECURSE gcda_files "${BUILD_DIR}/*.gcda")
if(NOT gcda_files)
    message(FATAL_ERROR "no .gcda files found — build with -DAPOLLO_COVERAGE=ON and run the tests first")
endif()

set(keys "")
foreach(gcda ${gcda_files})
    # Skip dependency compilation units outright, and the apollo_mcp_server
    # binary itself — it never runs under ctest, so its counters only show
    # static initializers and would drag every shared source towards 0%.
    if(gcda MATCHES "/_deps/" OR gcda MATCHES "apollo_mcp_server\\.dir")
        continue()
    endif()
    get_filename_component(obj_dir "${gcda}" DIRECTORY)
    execute_process(
        COMMAND gcov -n -o "${obj_dir}" "${gcda}"
        WORKING_DIRECTORY "${obj_dir}"
        OUTPUT_VARIABLE gcov_out
        ERROR_QUIET
    )
    # gcov -n output alternates:
    #   File '<path>'
    #   Lines executed:NN.NN% of M
    string(REPLACE "\n" ";" lines "${gcov_out}")
    set(current_file "")
    foreach(line ${lines})
        if(line MATCHES "^File '(.*)'")
            set(current_file "${CMAKE_MATCH_1}")
        elseif(line MATCHES "^Lines executed:([0-9.]+)% of ([0-9]+)")
            set(pct "${CMAKE_MATCH_1}")
            set(total "${CMAKE_MATCH_2}")
            if(current_file MATCHES "^${SOURCE_DIR}/(src|include|tests)/")
                file(RELATIVE_PATH rel "${SOURCE_DIR}" "${current_file}")
                string(MAKE_C_IDENTIFIER "${rel}" key)
                # Same file in several test binaries: keep the best-covered
                # entry (test units all ran; the max approximates the union).
                if((NOT DEFINED cov_pct_${key}) OR (pct GREATER cov_pct_${key}))
                    set(cov_pct_${key} "${pct}")
                    set(cov_total_${key} "${total}")
                    set(cov_name_${key} "${rel}")
                    list(APPEND keys "${key}")
                endif()
            endif()
            set(current_file "")
        endif()
    endforeach()
endforeach()

if(NOT keys)
    message(FATAL_ERROR "gcov produced no project-source entries")
endif()
list(REMOVE_DUPLICATES keys)

set(report "")
foreach(key ${keys})
    # Zero-pad the percentage so lexicographic sort == numeric sort.
    set(padded "000${cov_pct_${key}}")
    string(LENGTH "${padded}" plen)
    math(EXPR pstart "${plen} - 6")
    string(SUBSTRING "${padded}" ${pstart} 6 padded)
    list(APPEND report "${padded}% of ${cov_total_${key}} lines  ${cov_name_${key}}")
endforeach()

# Sort by coverage percentage — worst-covered files at the bottom, where a
# terminal leaves them visible.
list(SORT report)
list(REVERSE report)
message(STATUS "Line coverage by file (gcov):")
foreach(l ${report})
    message(STATUS "  ${l}")
endforeach()
