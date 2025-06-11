# CMake function to run the draft_parser tool
#
# This function runs the Python-based draft_parser tool to generate C++ source files
# from RFC draft specifications during the CMake configure step.
#
# Usage:
#   parse_draft(
#     RFC_FILE <path_to_rfc_file>
#     OUTPUT_NAME <output_file_name>
#     OUTPUT_DIR <output_directory>
#     [SETUP_VENV <ON|OFF|PATH>]
#   )
#
# Arguments:
#   RFC_FILE    - Path to the RFC draft file to parse
#   OUTPUT_NAME - Base name for output files (without extension)
#   OUTPUT_DIR  - Directory where generated files will be placed
#   SETUP_VENV  - Optional flag to create a virtual environment and install dependencies (ON/OFF/PATH)
#
# Generates:
#   ${OUTPUT_DIR}/${OUTPUT_NAME}.h
#   ${OUTPUT_DIR}/${OUTPUT_NAME}.cpp

set(DRAFT_PARSER_DIR "${CMAKE_CURRENT_LIST_DIR}/..")

function(parse_draft)
    # Ensure arguments
    set(options "")
    set(oneValueArgs RFC_FILE OUTPUT_NAME OUTPUT_DIR SETUP_VENV)
    set(multiValueArgs DEPENDS)
    cmake_parse_arguments(PARSE_DRAFT "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
    if(NOT PARSE_DRAFT_RFC_FILE)
        message(FATAL_ERROR "parse_draft: RFC_FILE argument is required")
    endif()
    if(NOT PARSE_DRAFT_OUTPUT_NAME)
        message(FATAL_ERROR "parse_draft: OUTPUT_NAME argument is required")
    endif()
    if(NOT PARSE_DRAFT_OUTPUT_DIR)
        message(FATAL_ERROR "parse_draft: OUTPUT_DIR argument is required")
    endif()
    
    # Paths
    set(DRAFT_PARSER_SCRIPT "${DRAFT_PARSER_DIR}/main.py")
    set(DRAFT_PARSER_REQUIREMENTS "${DRAFT_PARSER_DIR}/requirements.txt")
    if(NOT IS_ABSOLUTE "${PARSE_DRAFT_RFC_FILE}")
        set(RFC_FILE_PATH "${DRAFT_PARSER_DIR}/${PARSE_DRAFT_RFC_FILE}")
    else()
        set(RFC_FILE_PATH "${PARSE_DRAFT_RFC_FILE}")
    endif()
    
    # Create output directory
    file(MAKE_DIRECTORY "${PARSE_DRAFT_OUTPUT_DIR}")
    
    # Define output files
    set(OUTPUT_HEADER "${PARSE_DRAFT_OUTPUT_DIR}/${PARSE_DRAFT_OUTPUT_NAME}.h")
    set(OUTPUT_SOURCE "${PARSE_DRAFT_OUTPUT_DIR}/${PARSE_DRAFT_OUTPUT_NAME}.cpp")
    
    # Python setup
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    if(PARSE_DRAFT_SETUP_VENV AND PARSE_DRAFT_SETUP_VENV STREQUAL "ON")
        # We're managing python
        set(VENV_DIR "${CMAKE_CURRENT_BINARY_DIR}/venv")
        if(WIN32)
            set(PYTHON_EXECUTABLE "${VENV_DIR}/Scripts/python.exe")
            set(PIP_EXECUTABLE "${VENV_DIR}/Scripts/pip.exe")
        else()
            set(PYTHON_EXECUTABLE "${VENV_DIR}/bin/python")
            set(PIP_EXECUTABLE "${VENV_DIR}/bin/pip")
        endif()
        
        # Create virtual environment if it doesn't exist
        if(NOT EXISTS "${PYTHON_EXECUTABLE}")
            message(STATUS "Creating Python virtual environment for draft parser...")
            execute_process(
                COMMAND ${Python3_EXECUTABLE} -m venv "${VENV_DIR}"
                WORKING_DIRECTORY ${PARSE_DRAFT_OUTPUT_DIR}
                RESULT_VARIABLE VENV_RESULT
            )
            if(NOT VENV_RESULT EQUAL 0)
                message(FATAL_ERROR "Failed to create Python virtual environment")
            endif()
        endif()

        # Install dependencies if they don't exist
        execute_process(
            COMMAND ${PYTHON_EXECUTABLE} -c "import jinja2"
            RESULT_VARIABLE JINJA2_CHECK
            OUTPUT_QUIET
            ERROR_QUIET
        )
        if(NOT JINJA2_CHECK EQUAL 0)
            execute_process(
                COMMAND ${PIP_EXECUTABLE} install -r "${DRAFT_PARSER_REQUIREMENTS}"
                WORKING_DIRECTORY ${DRAFT_PARSER_DIR}
                RESULT_VARIABLE PIP_RESULT
            )
            if(NOT PIP_RESULT EQUAL 0)
                message(FATAL_ERROR "Failed to install Python dependencies")
            endif()
        endif()
    elseif(PARSE_DRAFT_SETUP_VENV AND NOT PARSE_DRAFT_SETUP_VENV STREQUAL "OFF")
        # Custom Python
        set(PYTHON_EXECUTABLE ${PARSE_DRAFT_SETUP_VENV})
        message(STATUS "Using custom Python executable: ${PYTHON_EXECUTABLE}")
        execute_process(
            COMMAND ${PYTHON_EXECUTABLE} -c "import jinja2"
            RESULT_VARIABLE JINJA2_CHECK
            OUTPUT_QUIET
            ERROR_QUIET
        )
        if(NOT JINJA2_CHECK EQUAL 0)
            message(WARNING "Jinja2 not found. You may need to install Python dependencies:\n ")
            message(FATAL_ERROR "  pip install -r ${DRAFT_PARSER_REQUIREMENTS}")
        endif()
    else()
        # System Python
        set(PYTHON_EXECUTABLE ${Python3_EXECUTABLE})
        execute_process(
            COMMAND ${PYTHON_EXECUTABLE} -c "import jinja2"
            RESULT_VARIABLE JINJA2_CHECK
            OUTPUT_QUIET
            ERROR_QUIET
        )
        if(NOT JINJA2_CHECK EQUAL 0)
            message(WARNING "Jinja2 not found. You may need to install Python dependencies:\n ")
            message(FATAL_ERROR "  pip install -r ${DRAFT_PARSER_REQUIREMENTS}")
        endif()
    endif()

    # Create custom command to generate the files
    add_custom_command(
        OUTPUT ${OUTPUT_HEADER} ${OUTPUT_SOURCE}
        COMMAND ${PYTHON_EXECUTABLE} ${DRAFT_PARSER_SCRIPT} 
                ${RFC_FILE_PATH} 
                "${PARSE_DRAFT_OUTPUT_DIR}/${PARSE_DRAFT_OUTPUT_NAME}"
        DEPENDS ${DRAFT_PARSER_SCRIPT} 
                ${RFC_FILE_PATH}
        WORKING_DIRECTORY ${DRAFT_PARSER_DIR}
        COMMENT "Generating MOQ messages: ${PARSE_DRAFT_RFC_FILE} -> ${PARSE_DRAFT_OUTPUT_NAME}"
        VERBATIM
    )
    
    # Create a target for the generated files
    set(TARGET_NAME "generate_moq_messages")
    add_custom_target(${TARGET_NAME}
        DEPENDS ${OUTPUT_HEADER} ${OUTPUT_SOURCE}
        COMMENT "Generated MOQ messages: ${PARSE_DRAFT_OUTPUT_NAME}"
    )
    
    # Outputs
    set(${PARSE_DRAFT_OUTPUT_NAME}_HEADER ${OUTPUT_HEADER} PARENT_SCOPE)
    set(${PARSE_DRAFT_OUTPUT_NAME}_SOURCE ${OUTPUT_SOURCE} PARENT_SCOPE)
    set(${PARSE_DRAFT_OUTPUT_NAME}_TARGET ${TARGET_NAME} PARENT_SCOPE)
endfunction()