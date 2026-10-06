execute_process(
    COMMAND git describe --tags --match "v[0-9]*.[0-9]*" --dirty
    WORKING_DIRECTORY ${SOURCE_DIR}
    OUTPUT_VARIABLE KAMORA_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
string(REGEX REPLACE "^v" "" KAMORA_VERSION "${KAMORA_VERSION}")
if(NOT KAMORA_VERSION)
    set(KAMORA_VERSION unknown)
endif()

configure_file(${INPUT} ${OUTPUT} @ONLY)
