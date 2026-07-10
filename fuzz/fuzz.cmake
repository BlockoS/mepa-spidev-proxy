# Copyright (c) 2026 Vincent Jardin, Vincent Cruz, Free Mobile
# SPDX-License-Identifier: BSD-4-Clause
#
# Fuzzing targets, included from the root CMakeLists (guarded by SPIPROXY_FUZZ)
# after the executables so the harness inherits the project-wide compile
# flags. The SPIPROXY_FUZZ[_COVERAGE] options and their global build-flag
# interactions (SPIPROXY_STATIC exclusion, no _FORTIFY_SOURCE under fuzz) stay
# in the root file, which must evaluate them early. Paths are relative to the
# project root -- include() keeps CMAKE_CURRENT_SOURCE_DIR pointed there.

if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR
        "Fuzzing (-fsanitize=fuzzer) is only available with Clang. "
        "Reconfigure your project with CMAKE_C_COMPILER=clang")
endif()

# Harness: #includes lan80xx_spid.c (SPIPROXY_FUZZ drops main + stubs the SPI
# backend). Needs clang for -fsanitize=fuzzer.
add_executable(fuzz_msg fuzz/fuzz_msg.c log.c parse.c)
target_include_directories(fuzz_msg PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(fuzz_msg PRIVATE SPIPROXY_FUZZ)
target_compile_options(fuzz_msg PRIVATE
    -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer
    -Wno-unused-function)
target_link_options(fuzz_msg PRIVATE -fsanitize=fuzzer,address,undefined)
set_target_properties(fuzz_msg PROPERTIES FOLDER fuzz)

# Optional: clang source-based coverage. `fuzz_coverage` replays the corpus
# through the harness and prints an llvm-cov report for the parser.
if(SPIPROXY_FUZZ_COVERAGE)
    find_program(LLVM_PROFDATA NAMES llvm-profdata llvm-profdata-21
                 llvm-profdata-20 llvm-profdata-19 llvm-profdata-18)
    find_program(LLVM_COV NAMES llvm-cov llvm-cov-21 llvm-cov-20
                 llvm-cov-19 llvm-cov-18)
    if(NOT LLVM_PROFDATA OR NOT LLVM_COV)
        message(FATAL_ERROR
            "SPIPROXY_FUZZ_COVERAGE needs llvm-profdata and llvm-cov")
    endif()
    target_compile_options(fuzz_msg PRIVATE
        -fprofile-instr-generate -fcoverage-mapping)
    target_link_options(fuzz_msg PRIVATE -fprofile-instr-generate)
    set(SPIPROXY_FUZZ_CORPUS "${CMAKE_CURRENT_SOURCE_DIR}/fuzz/corpus"
        CACHE PATH "Corpus directory to replay for the coverage report")
    # Replay the corpus once into fuzz_msg.profdata, shared by the reports.
    add_custom_target(fuzz_profdata
        COMMAND ${CMAKE_COMMAND} -E env
                ASAN_OPTIONS=symbolize=0 UBSAN_OPTIONS=symbolize=0
                LLVM_PROFILE_FILE=fuzz_msg.profraw
                $<TARGET_FILE:fuzz_msg> -runs=0 -print_funcs=0
                "${SPIPROXY_FUZZ_CORPUS}"
        COMMAND ${LLVM_PROFDATA} merge -sparse fuzz_msg.profraw
                -o fuzz_msg.profdata
        DEPENDS fuzz_msg
        BYPRODUCTS fuzz_msg.profraw fuzz_msg.profdata
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        USES_TERMINAL VERBATIM
        COMMENT "Replaying ${SPIPROXY_FUZZ_CORPUS} -> fuzz_msg.profdata")

    # Terminal summary.
    add_custom_target(fuzz_coverage
        COMMAND ${LLVM_COV} report $<TARGET_FILE:fuzz_msg>
                -instr-profile=fuzz_msg.profdata
                "${CMAKE_CURRENT_SOURCE_DIR}/lan80xx_spid.c"
        DEPENDS fuzz_profdata
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        USES_TERMINAL VERBATIM
        COMMENT "llvm-cov summary for lan80xx_spid.c")

    # Annotated line-by-line HTML.
    add_custom_target(fuzz_coverage_html
        COMMAND ${LLVM_COV} show $<TARGET_FILE:fuzz_msg>
                -instr-profile=fuzz_msg.profdata -format=html
                -output-dir=${CMAKE_CURRENT_BINARY_DIR}/coverage-html
                "${CMAKE_CURRENT_SOURCE_DIR}/lan80xx_spid.c"
        COMMAND ${CMAKE_COMMAND} -E echo
                "HTML report: ${CMAKE_CURRENT_BINARY_DIR}/coverage-html/index.html"
        DEPENDS fuzz_profdata
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        USES_TERMINAL VERBATIM
        COMMENT "llvm-cov HTML report for lan80xx_spid.c")
    set_target_properties(fuzz_profdata fuzz_coverage fuzz_coverage_html
                          PROPERTIES FOLDER fuzz)
endif()
