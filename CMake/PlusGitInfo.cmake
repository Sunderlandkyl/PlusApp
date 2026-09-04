# plus_git_info(<PREFIX>)
#
# Reads the current commit out of the repository and sets, in the caller's
# scope:
#   <PREFIX>_REVISION              full commit hash
#   <PREFIX>_SHORT_REVISION        abbreviated commit hash
#   <PREFIX>_COMMIT_DATE           commit date as YYYY-MM-DD
#   <PREFIX>_COMMIT_DATE_NO_DASHES the same date as YYYYMMDD
#
# All four fall back to "NA". The version this replaces left the two date
# variables undefined when git was missing, and ran an unguarded regex over
# the output of "git show", which is a hard configure error when that output
# is empty: a shallow clone, an export with no .git, or a git that failed for
# any other reason. The package file name embeds the date, so an empty value
# there produces a mis-named package.

include_guard(GLOBAL)

function(plus_git_info prefix)
  set(_revision "NA")
  set(_short_revision "NA")
  set(_commit_date "NA")
  set(_commit_date_no_dashes "NA")

  if(NOT PLUS_OFFLINE_BUILD AND NOT PLUSAPP_OFFLINE_BUILD AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    find_package(Git QUIET)
    if(Git_FOUND)
      _plus_git_output(_result _output rev-parse HEAD)
      if(_result EQUAL 0 AND _output)
        set(_revision "${_output}")
      endif()

      _plus_git_output(_result _output rev-parse --short HEAD)
      if(_result EQUAL 0 AND _output)
        set(_short_revision "${_output}")
      endif()

      # %cd with --date=short prints exactly YYYY-MM-DD, so no parsing of the
      # full commit message is needed.
      _plus_git_output(_result _output log -1 --format=%cd --date=short)
      if(_result EQUAL 0 AND _output MATCHES "^[0-9]+-[0-9]+-[0-9]+$")
        set(_commit_date "${_output}")
        string(REPLACE "-" "" _commit_date_no_dashes "${_commit_date}")
      endif()
    else()
      message(STATUS "Git was not found, so no revision information is available.")
    endif()
  endif()

  set(${prefix}_REVISION "${_revision}" PARENT_SCOPE)
  set(${prefix}_SHORT_REVISION "${_short_revision}" PARENT_SCOPE)
  set(${prefix}_COMMIT_DATE "${_commit_date}" PARENT_SCOPE)
  set(${prefix}_COMMIT_DATE_NO_DASHES "${_commit_date_no_dashes}" PARENT_SCOPE)
  message(STATUS "Building revision ${_short_revision} of ${prefix} (${_commit_date})")
endfunction()

# Run git in the source tree and return its trimmed output plus the exit code.
function(_plus_git_output result_var output_var)
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" ${ARGN}
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _output
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
    )
  set(${result_var} "${_result}" PARENT_SCOPE)
  set(${output_var} "${_output}" PARENT_SCOPE)
endfunction()
