cmake_minimum_required(VERSION 3.27)
project(faloya C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

add_executable(faloya
    src/main.cpp
    src/process.cpp
    src/version.cpp
    src/process.h
    src/progress.cpp
    src/progress.h
    src/buildfile.h
    src/toolchain.h
    src/build.h
    src/packages.h
    src/json.h
    src/native.h
    src/lsp_rpc.h
    src/lsp_analysis.h
    src/lsp_workspace.h
    src/lsp_features.h
    src/lsp.h
    src/format.h
    src/builtin.h
    src/includes.h
    src/utils.h
    src/small_vector.h
    src/flat_map.h
    src/lexer.h
    src/ast.h
    src/dump.h
    src/clone.h
    src/parser.h
    src/resolve.h
    src/expansion_bind.h
    src/typecheck_expansion.h
    src/builtins.h
    src/typecheck.h
    src/type_queries.h
    src/typecheck_types.h
    src/typecheck_exprs.h
    src/typecheck_flow.h
    src/typecheck_calls.h
    src/typecheck_builtins.h
    src/typecheck_nodes.h
    src/optimize.h
    src/optimize_basecase.h
    src/optimize_tre.h
    src/optimize_loops.h
    src/optimize_reassoc.h
    src/bce.h
    src/codegen.h
    src/codegen_types.h
    src/codegen_frames.h
    src/codegen_values.h
    src/codegen_construct.h
    src/codegen_stmts.h
    src/codegen_calls.h
    src/codegen_builtins.h
    src/codegen_render.h
    src/codegen_emit.h
    src/codegen_stacks.h
    src/codegen_nodes.h
    src/runtime_inline.h
    src/jit.h
    src/runtime/runtime.h
    src/runtime/runtime_impl.h
    src/runtime/runtime_threads.h
    src/runtime/runtime_ext.h
    src/runtime/runtime_os.h
    src/runtime/faloya.h
    src/gfx.h
)


if(MSVC)
    target_sources(faloya PRIVATE src/faloya.manifest)
elseif(WIN32)
    enable_language(RC)
    target_sources(faloya PRIVATE src/faloya.rc)
    set_property(SOURCE src/faloya.rc APPEND PROPERTY OBJECT_DEPENDS
                 "${CMAKE_CURRENT_SOURCE_DIR}/src/faloya.manifest")
endif()


set(FALOYA_VERSION "" CACHE STRING "The compiler's version; empty for git describe")
set(faloya_version_h "${CMAKE_CURRENT_BINARY_DIR}/version/faloya_version.h")
add_custom_target(faloya_version
    COMMAND "${CMAKE_COMMAND}" "-DSRC=${CMAKE_CURRENT_SOURCE_DIR}" "-DOUT=${faloya_version_h}"
            "-DVERSION=${FALOYA_VERSION}" -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.cmake"
    BYPRODUCTS "${faloya_version_h}")
add_dependencies(faloya faloya_version)
target_include_directories(faloya PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/version")


find_package(Threads REQUIRED)
target_link_libraries(faloya PRIVATE Threads::Threads)

target_include_directories(faloya PRIVATE third_party/subprocess)


set(faloya_main_stack 67108864)
if(MSVC)
    target_link_options(faloya PRIVATE "/STACK:${faloya_main_stack}")
elseif(MINGW)
    
    target_link_options(faloya PRIVATE "LINKER:--stack,${faloya_main_stack}" -static -static-libgcc -static-libstdc++)
elseif(APPLE)
    math(EXPR faloya_main_stack_hex "${faloya_main_stack}" OUTPUT_FORMAT HEXADECIMAL)
    target_link_options(faloya PRIVATE "LINKER:-stack_size,${faloya_main_stack_hex}")
endif()


add_library(faloya_shaderc STATIC src/shaderc.c src/shaderc.h src/gfx_blob.h)
target_include_directories(faloya_shaderc PRIVATE third_party/cute_spirv)
if(MSVC)
    
    
    target_compile_options(faloya_shaderc PRIVATE /W0 /std:clatest /experimental:c11atomics)
else()
    set_target_properties(faloya_shaderc PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)
    target_compile_options(faloya_shaderc PRIVATE -w)
endif()
target_link_libraries(faloya PRIVATE faloya_shaderc)


include(cmake/libtcc.cmake)
target_link_libraries(faloya PRIVATE libtcc)
add_dependencies(faloya tcc_runtime)


set(FALOYA_TCC_LIB_PATH "${TCC_STAGE}" CACHE PATH "TinyCC's runtime directory")
target_compile_definitions(faloya PRIVATE FALOYA_TCC_LIB_PATH="${FALOYA_TCC_LIB_PATH}")


set(FALOYA_STDLIB_PATH "${CMAKE_CURRENT_SOURCE_DIR}/stdlib" CACHE PATH "The standard library's directory")
target_compile_definitions(faloya PRIVATE FALOYA_STDLIB_PATH="${FALOYA_STDLIB_PATH}")


foreach(header stdint.h string.h stdlib.h math.h)
    configure_file(src/runtime/jit_c.h "${TCC_STAGE}/faloya/include/${header}" COPYONLY)
endforeach()


set(jit_symbols "")
foreach(header runtime.h runtime_ext.h)
    set(path "${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/${header}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${path}")
    file(STRINGS "${path}" declarations REGEX "^GS_API ")
    foreach(declaration IN LISTS declarations)
        if(declaration MATCHES "(gs_[A-Za-z0-9_]+)\\(")
            string(APPEND jit_symbols "GS_JIT_SYMBOL(${CMAKE_MATCH_1})\n")
        endif()
    endforeach()
endforeach()
file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/jit_symbols.inc" CONTENT "${jit_symbols}" @ONLY)
target_sources(faloya PRIVATE src/jit_runtime.c src/jit_runtime.h)
target_include_directories(faloya PRIVATE "${CMAKE_BINARY_DIR}")
set_property(TARGET faloya PROPERTY C_STANDARD 11)


set(FALOYA_BUILTIN_LIBS "" CACHE STRING
    "Blessed packages to compile into the compiler: all, or a list of their names")
include(cmake/package.cmake)
faloya_builtin_packages("${FALOYA_BUILTIN_LIBS}")
if(MSVC AND TARGET faloya_gfx)
    
    
    
    foreach(dll IN ITEMS user32 gdi32 winmm imm32 ole32 oleaut32 version setupapi shell32)
        target_link_options(faloya PRIVATE "/DELAYLOAD:${dll}.dll")
    endforeach()
    target_link_libraries(faloya PRIVATE delayimp)
endif()

if(MSVC)
    
    
    target_compile_options(faloya PRIVATE /W4 /permissive- /utf-8 /EHsc /bigobj)
    target_compile_definitions(faloya PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    
    
    
    target_compile_options(faloya PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
endif()

option(FALOYA_BUILD_LSP_TESTS "Build deterministic language server tests" OFF)
if(FALOYA_BUILD_LSP_TESTS)
    get_target_property(faloya_lsp_sources faloya SOURCES)
    list(REMOVE_ITEM faloya_lsp_sources src/main.cpp)
    add_executable(faloya_lsp_test test/lsp_server.cpp ${faloya_lsp_sources})
    foreach(prop INCLUDE_DIRECTORIES COMPILE_DEFINITIONS COMPILE_OPTIONS LINK_OPTIONS LINK_LIBRARIES)
        set_property(TARGET faloya_lsp_test PROPERTY ${prop} "$<TARGET_PROPERTY:faloya,${prop}>")
    endforeach()
    add_dependencies(faloya_lsp_test tcc_runtime)
    enable_testing()
    add_test(NAME lsp_call_hierarchy COMMAND faloya_lsp_test
        "${CMAKE_CURRENT_SOURCE_DIR}/stdlib" "${CMAKE_CURRENT_BINARY_DIR}/lsp_calls.fal")
endif()
