# Runs a command-line tool with --help and captures the output.
#
# Invoked through "cmake -P" from a POST_BUILD step, because
# add_custom_command does not run a shell and so cannot redirect with ">".
#
# Expects PLUS_HELP_EXECUTABLE and PLUS_HELP_OUTPUT to be given with -D.

get_filename_component(_output_dir "${PLUS_HELP_OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${_output_dir}")

execute_process(
  COMMAND "${PLUS_HELP_EXECUTABLE}" --help
  OUTPUT_FILE "${PLUS_HELP_OUTPUT}"
  ERROR_VARIABLE _error
  RESULT_VARIABLE _result
  )

# Generating documentation must not fail the build.
if(NOT _result EQUAL 0)
  message(STATUS "Could not generate help for ${PLUS_HELP_EXECUTABLE}: ${_error}")
endif()
