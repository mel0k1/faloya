


if(NOT VERSION)
    execute_process(COMMAND git describe --tags --always --dirty
                    WORKING_DIRECTORY "${SRC}"
                    OUTPUT_VARIABLE VERSION OUTPUT_STRIP_TRAILING_WHITESPACE
                    RESULT_VARIABLE result ERROR_QUIET)
    if(NOT result EQUAL 0 OR NOT VERSION)
        set(VERSION "unknown")
    endif()
endif()
set(text "#define FALOYA_VERSION \"${VERSION}\"\n")
set(old "")
if(EXISTS "${OUT}")
    file(READ "${OUT}" old)
endif()
if(NOT old STREQUAL text)
    file(WRITE "${OUT}" "${text}")
endif()
