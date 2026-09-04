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

  # The offscreen plugin as well, when Qt ships one. It costs a few kilobytes
  # and is what makes the package usable over a connection with no display, by
  # a test harness or on a machine with no X server at all.
  set(_plugins ${_plugin})
  if(TARGET Qt5::QOffscreenIntegrationPlugin)
    list(APPEND _plugins Qt5::QOffscreenIntegrationPlugin)
  endif()

  # Qt looks for plugins in a "platforms" directory beside the executable.
  foreach(_each IN LISTS _plugins)
    install(FILES $<TARGET_FILE:${_each}>
      DESTINATION ${PLUSAPP_INSTALL_BIN_DIR}/platforms
      COMPONENT RuntimeLibraries
      )
    set_property(GLOBAL APPEND PROPERTY PLUSAPP_RUNTIME_MODULES $<TARGET_FILE:${_each}>)
  endforeach()
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

  set(_runtime_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

  # Every one of these lists is written verbatim into cmake_install.cmake, so
  # each entry has to arrive there quoted. An unquoted regex containing a
  # parenthesis or a semicolon is a syntax error in the generated script, and
  # an unquoted path breaks on the first space.
  foreach(_list IN ITEMS _executables _modules _pre_exclude _post_exclude)
    set(${_list}_arguments "")
    foreach(_entry IN LISTS ${_list})
      string(APPEND ${_list}_arguments " \"${_entry}\"")
    endforeach()
  endforeach()

  install(CODE "
    # Normalize paths before matching them against each other. Without this a
    # macOS framework reached through two different chains of \"..\" segments
    # looks like two libraries with one name, and the scan stops with
    # \"Multiple conflicting paths found\". Guarded because the policy does not
    # exist in every CMake this project supports.
    if(POLICY CMP0207)
      cmake_policy(SET CMP0207 NEW)
    endif()
    set(_plus_executables${_executables_arguments})

    # PlusLib's tools are packaged alongside PlusApp's and are built into the
    # same directory, but they are not targets here, so they cannot be named.
    # Nothing else drags in what only they need: a package built from the
    # applications alone was missing libvtkIOImage, which PlusServer loads and
    # no application does. Collect them from the build tree at install time,
    # when they all exist. On Unix an executable carries no extension, which
    # tells them apart from the configuration files and logs that share the
    # directory.
    set(_plus_candidates)
    if(NOT \"${_runtime_directory}\" STREQUAL \"\")
      file(GLOB _plus_candidates \"${_runtime_directory}/*\")
    endif()
    foreach(_plus_candidate IN LISTS _plus_candidates)
      get_filename_component(_plus_name \"\${_plus_candidate}\" NAME)
      if(NOT IS_DIRECTORY \"\${_plus_candidate}\" AND NOT _plus_name MATCHES \"[.]\")
        list(APPEND _plus_executables \"\${_plus_candidate}\")
      endif()
    endforeach()
    list(REMOVE_DUPLICATES _plus_executables)

    file(GET_RUNTIME_DEPENDENCIES
      EXECUTABLES \${_plus_executables}
      MODULES${_modules_arguments}
      RESOLVED_DEPENDENCIES_VAR _resolved
      UNRESOLVED_DEPENDENCIES_VAR _unresolved
      PRE_EXCLUDE_REGEXES${_pre_exclude_arguments}
      POST_EXCLUDE_REGEXES${_post_exclude_arguments}
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
