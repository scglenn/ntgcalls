function(GitClone)
    cmake_parse_arguments(ARG "" "" "URL;COMMIT;DIRECTORY" ${ARGN})
    string(REPLACE "https" "" GIT_CACHE ${ARG_URL})
    string(REPLACE "http" "" GIT_CACHE ${GIT_CACHE})
    string(REPLACE "//" "" GIT_CACHE ${GIT_CACHE})
    string(REPLACE ":" "" GIT_CACHE ${GIT_CACHE})
    string(REPLACE "/" "_" GIT_CACHE ${GIT_CACHE})
    string(REPLACE ".git" "" GIT_CACHE ${GIT_CACHE})
    string(REPLACE "." "_" GIT_CACHE ${GIT_CACHE})
    string(REPLACE "-" "_" GIT_CACHE ${GIT_CACHE})
    string(TOUPPER ${GIT_CACHE} GIT_CACHE)

    if (NOT "${${GIT_CACHE}}" STREQUAL "${ARG_COMMIT}${ARG_DIRECTORY}" AND EXISTS ${ARG_DIRECTORY})
        message(STATUS "Cleaning repo ${ARG_DIRECTORY}")
        file(REMOVE_RECURSE ${ARG_DIRECTORY})
    endif ()
    if (NOT EXISTS ${ARG_DIRECTORY})
        message(STATUS "Cloning ${ARG_URL}")
        file(MAKE_DIRECTORY ${ARG_DIRECTORY})
        execute_process(
                COMMAND git init
                WORKING_DIRECTORY ${ARG_DIRECTORY}
                RESULT_VARIABLE GIT_RESULT
                OUTPUT_QUIET
                ERROR_QUIET
        )
        if (NOT GIT_RESULT EQUAL 0)
            message(FATAL_ERROR "The git repository can not be initialized")
        endif ()
        execute_process(
                COMMAND git remote add origin ${ARG_URL}
                WORKING_DIRECTORY ${ARG_DIRECTORY}
                RESULT_VARIABLE GIT_RESULT
                OUTPUT_QUIET
                ERROR_QUIET
        )
        if (NOT GIT_RESULT EQUAL 0)
            message(FATAL_ERROR "The remote origin could not be added")
        endif ()
        execute_process(
                COMMAND git fetch --depth=1 origin ${ARG_COMMIT}
                WORKING_DIRECTORY ${ARG_DIRECTORY}
                RESULT_VARIABLE GIT_RESULT
                OUTPUT_QUIET
                ERROR_QUIET
        )
        if (NOT GIT_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to fetch from remote origin.")
        endif ()
        execute_process(
                COMMAND git reset --hard FETCH_HEAD
                WORKING_DIRECTORY ${ARG_DIRECTORY}
                RESULT_VARIABLE GIT_RESULT
                OUTPUT_QUIET
                ERROR_QUIET
        )
        if (NOT GIT_RESULT EQUAL 0)
            message(FATAL_ERROR "Unable to reset to the fetched commit.")
        endif ()
        set(${GIT_CACHE} ${ARG_COMMIT}${ARG_DIRECTORY} CACHE STRING "Git cache installation for ${ARG_URL}" FORCE)
    endif ()
endfunction()

function(GitFile)
    cmake_parse_arguments(ARG "" "" "URL;DIRECTORY" ${ARGN})
    if ("${ARG_URL}" MATCHES "googlesource.com")
        set(BASE64 TRUE)
        set(ARG_URL ${ARG_URL}?format=text)
    endif ()
    execute_process(
        COMMAND curl -s ${ARG_URL}
        RESULT_VARIABLE GIT_RESULT_CODE
        OUTPUT_VARIABLE FILE_CONTENT
    )
    if(NOT GIT_RESULT_CODE EQUAL 0)
        message(FATAL_ERROR "Failed to fetch from remote origin.")
    endif ()
    if (BASE64)
        string(REPLACE "\n" "" FILE_CONTENT "${FILE_CONTENT}")
        string(REPLACE "\r" "" FILE_CONTENT "${FILE_CONTENT}")
        set(_GITFILE_B64_PATH "${CMAKE_CURRENT_BINARY_DIR}/gitfile_base64.tmp")
        file(WRITE "${_GITFILE_B64_PATH}" "${FILE_CONTENT}")
        execute_process(
            COMMAND ${Python_EXECUTABLE} -c "import base64,sys; data=sys.stdin.buffer.read(); sys.stdout.buffer.write(base64.b64decode(data))"
            INPUT_FILE "${_GITFILE_B64_PATH}"
            OUTPUT_FILE ${ARG_DIRECTORY}
            RESULT_VARIABLE GIT_RESULT_CODE
        )
        file(REMOVE "${_GITFILE_B64_PATH}")
        if(NOT GIT_RESULT_CODE EQUAL 0)
            message(FATAL_ERROR "Failed to decrypt git file")
        endif ()
        return()
    endif ()
    file(WRITE ${ARG_DIRECTORY} "${FILE_CONTENT}")
endfunction()