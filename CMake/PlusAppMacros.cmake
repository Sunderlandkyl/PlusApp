FUNCTION(AddPlusQt5Executable _exec_name)
  LIST(POP_FRONT ARGV)
  ADD_EXECUTABLE(${_exec_name} ${ARGV})

  set(PLUSAPP_QT_COMPONENTS_ARGS)
  IF (PLUSAPP_QT_COMPONENTS)
    # Ensure that all required Qt components are specified as arugments to windeployqt
    # Ex. "--core --multimedia --test" etc.
    foreach (COMPONENT ${PLUSAPP_QT_COMPONENTS})
      string(TOLOWER ${COMPONENT} COMPONENT_LOWER)
      list(APPEND PLUSAPP_QT_COMPONENTS_ARGS --${COMPONENT_LOWER})
    endforeach(COMPONENT ${PLUSAPP_QT_COMPONENTS})
  ENDIF()

  IF(TARGET Qt5::windeployqt)
      # execute windeployqt in a tmp directory after build
      ADD_CUSTOM_COMMAND(TARGET ${_exec_name}
          POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMAKE_CURRENT_BINARY_DIR}/windeployqt"
          COMMAND Qt5::windeployqt ${PLUSAPP_QT_COMPONENTS_ARGS} --dir "${CMAKE_CURRENT_BINARY_DIR}/windeployqt" "$<TARGET_FILE_DIR:${_exec_name}>/$<TARGET_FILE_NAME:${_exec_name}>"
          COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_BINARY_DIR}/windeployqt $<TARGET_FILE_DIR:${_exec_name}>
      )

      # copy deployment directory during installation
      INSTALL(
          DIRECTORY
          "${CMAKE_CURRENT_BINARY_DIR}/windeployqt/"
          DESTINATION ${PLUSAPP_INSTALL_BIN_DIR}
          COMPONENT RuntimeLibraries
      )
  ENDIF()
ENDFUNCTION()

# plus_install_qt_runtime(<executable target>...)
#
# Put the Qt platform plugin into the package. Everything Qt-related used to
# happen inside AddPlusQt5Executable, guarded on the Qt5::windeployqt target,
# which only exists on Windows; a Linux or macOS package therefore shipped
# without Qt at all.
function(plus_install_qt_runtime)
  if(WIN32)
    # windeployqt already collects the plugins, from AddPlusQt5Executable.
    return()
  endif()

  if(APPLE)
    set(_plugin Qt5::QCocoaIntegrationPlugin)
  else()
    set(_plugin Qt5::QXcbIntegrationPlugin)
  endif()

  if(NOT TARGET ${_plugin})
    message(WARNING "The Qt platform plugin ${_plugin} was not found, so the package will not be able to open a window.")
    return()
  endif()

  # Qt looks for plugins in a "platforms" directory beside the executable.
  install(FILES $<TARGET_FILE:${_plugin}>
    DESTINATION ${PLUSAPP_INSTALL_BIN_DIR}/platforms
    COMPONENT RuntimeLibraries
    )
  set_property(GLOBAL APPEND PROPERTY PLUSAPP_RUNTIME_MODULES $<TARGET_FILE:${_plugin}>)
endfunction()

# plus_register_runtime_executable(<target>...)
#
# Record an executable whose shared library dependencies have to travel with
# the package.
function(plus_register_runtime_executable)
  foreach(_target IN LISTS ARGN)
    if(TARGET ${_target})
      set_property(GLOBAL APPEND PROPERTY PLUSAPP_RUNTIME_EXECUTABLES $<TARGET_FILE:${_target}>)
    endif()
  endforeach()
endfunction()

# plus_install_runtime_dependencies()
#
# Collect the shared libraries the built executables actually load and install
# them alongside. Called once, after every target has been defined.
#
# On Windows the package is assembled from the install rules of the projects
# that were built, plus windeployqt; that mechanism is left alone. Everywhere
# else there was no mechanism at all, so the package contained executables and
# nothing they link against.
#
# The closure is computed from the build tree, whose binaries still carry
# their build RPATH, so VTK, ITK, IGSIO, OpenIGTLink and Qt are all reachable
# without naming any of their directories here.
function(plus_install_runtime_dependencies)
  if(WIN32)
    return()
  endif()

  get_property(_executables GLOBAL PROPERTY PLUSAPP_RUNTIME_EXECUTABLES)
  get_property(_modules GLOBAL PROPERTY PLUSAPP_RUNTIME_MODULES)
  if(NOT _executables AND NOT _modules)
    return()
  endif()

  # Libraries that belong to the operating system, the graphics stack or the
  # windowing system. Shipping these makes a package that refuses to run on
  # any machine whose versions differ.
  set(_pre_exclude
    "^lib(c|m|dl|rt|pthread|util|resolv|gcc_s|stdc[+][+])[.]"
    "^ld-linux"
    "^libSystem"
    )
  set(_post_exclude
    "^/lib" "^/lib64" "^/usr/lib" "^/usr/lib64" "^/System/"
    "libGL" "libGLX" "libEGL" "libOpenGL" "libGLdispatch"
    "libX11" "libXext" "libXrender" "libxcb" "libxkbcommon"
    "libfontconfig" "libfreetype" "libdbus-1" "libglib" "libgobject"
    "libz[.]" "libexpat" "libuuid" "libdrm"
    )

  install(CODE "
    file(GET_RUNTIME_DEPENDENCIES
      EXECUTABLES ${_executables}
      MODULES ${_modules}
      RESOLVED_DEPENDENCIES_VAR _resolved
      UNRESOLVED_DEPENDENCIES_VAR _unresolved
      PRE_EXCLUDE_REGEXES ${_pre_exclude}
      POST_EXCLUDE_REGEXES ${_post_exclude}
      )
    if(_unresolved)
      message(WARNING \"These runtime dependencies were not found and are not in the package: \${_unresolved}\")
    endif()
    foreach(_dependency IN LISTS _resolved)
      if(_dependency MATCHES \"^(.*[.]framework)/\")
        # Keep a macOS framework whole, minus its headers.
        file(INSTALL \"\${CMAKE_MATCH_1}\"
          DESTINATION \"\${CMAKE_INSTALL_PREFIX}/${PLUSAPP_INSTALL_LIB_DIR}\"
          USE_SOURCE_PERMISSIONS PATTERN Headers EXCLUDE)
      else()
        file(INSTALL \"\${_dependency}\"
          DESTINATION \"\${CMAKE_INSTALL_PREFIX}/${PLUSAPP_INSTALL_LIB_DIR}\"
          FOLLOW_SYMLINK_CHAIN)
      endif()
    endforeach()"
    COMPONENT RuntimeLibraries
    )
endfunction()
