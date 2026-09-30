# Runs calc with the arguments in <CASE>.args (one per line) and compares stdout with
# <CASE>.out (or <CASE>.<LONG_DOUBLE_BITS>.out), stderr with <CASE>.err (empty if absent) and the
# exit code with <CASE>.code (0 if absent). <CASE>.in, if present, is standard input.
string(REPLACE "|" ";" emulator "${EMULATOR}")
file(STRINGS "${CASE}.args" args ENCODING UTF-8)
set(input)
if(EXISTS "${CASE}.in")
    set(input INPUT_FILE "${CASE}.in")
endif()
execute_process(COMMAND ${emulator} "${CALC}" ${args} ${input}
                OUTPUT_VARIABLE out ERROR_VARIABLE err RESULT_VARIABLE code)

set(expected "${CASE}.out")
if(NOT EXISTS "${expected}")
    set(expected "${CASE}.${LONG_DOUBLE_BITS}.out")
endif()
file(READ "${expected}" want)
if(NOT out STREQUAL want)
    message(FATAL_ERROR "stdout differs from ${expected}\n--- expected\n${want}--- actual\n${out}")
endif()

set(want_err "")
if(EXISTS "${CASE}.err")
    file(READ "${CASE}.err" want_err)
endif()
if(NOT err STREQUAL want_err)
    message(FATAL_ERROR "stderr differs\n--- expected\n${want_err}--- actual\n${err}")
endif()

set(want_code 0)
if(EXISTS "${CASE}.code")
    file(STRINGS "${CASE}.code" want_code)
endif()
if(NOT code EQUAL want_code)
    message(FATAL_ERROR "exit code ${code}, expected ${want_code}")
endif()
