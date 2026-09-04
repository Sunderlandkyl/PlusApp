IF("${PLUSAPP_PACKAGE_EDITION}" STREQUAL "")
  SET(PLUSAPP_PACKAGE_EDITION_PLATFORM "${PLUSLIB_PLATFORM}")
ELSE()
  SET(PLUSAPP_PACKAGE_EDITION_PLATFORM "${PLUSAPP_PACKAGE_EDITION}-${PLUSLIB_PLATFORM}")
ENDIF()

#-----------------------------------------------------------------------------
# Installation vars.
# PLUSAPP_INSTALL_BIN_DIR          - binary dir(executables)
# PLUSAPP_INSTALL_LIB_DIR          - library dir(libs)
# PLUSAPP_INSTALL_DATA_DIR         - share dir(say, examples, data, etc)
# PLUSAPP_INSTALL_CONFIG_DIR       - config dir(configuration files)
# PLUSAPP_INSTALL_SCRIPTS_DIR      - scripts dir
# PLUSAPP_INSTALL_INCLUDE_DIR      - include dir(headers)
# PLUSAPP_INSTALL_PACKAGE_DIR      - package/export configuration files
# PLUSAPP_INSTALL_NO_DEVELOPMENT   - do not install development files
# PLUSAPP_INSTALL_NO_RUNTIME       - do not install runtime files
# PLUSAPP_INSTALL_NO_DOCUMENTATION - do not install documentation files
# Applications
# RuntimeLibraries
# Development

IF(NOT PLUSAPP_INSTALL_BIN_DIR)
  SET(PLUSAPP_INSTALL_BIN_DIR "bin")
ENDIF()

IF(NOT PLUSAPP_INSTALL_LIB_DIR)
  SET(PLUSAPP_INSTALL_LIB_DIR "lib")
ENDIF()

IF(NOT PLUSAPP_INSTALL_DATA_DIR)
  SET(PLUSAPP_INSTALL_DATA_DIR "data")
ENDIF()

IF(NOT PLUSAPP_INSTALL_CONFIG_DIR)
  SET(PLUSAPP_INSTALL_CONFIG_DIR "config")
ENDIF()

IF(NOT PLUSAPP_INSTALL_SCRIPTS_DIR)
  SET(PLUSAPP_INSTALL_SCRIPTS_DIR "scripts")
ENDIF()

IF(NOT PLUSAPP_INSTALL_INCLUDE_DIR)
  SET(PLUSAPP_INSTALL_INCLUDE_DIR "include")
ENDIF()

IF(NOT PLUSAPP_INSTALL_DOCUMENTATION_DIR)
  SET(PLUSAPP_INSTALL_DOCUMENTATION_DIR "doc")
ENDIF()

IF(NOT PLUSAPP_INSTALL_NO_DOCUMENTATION)
  SET(PLUSAPP_INSTALL_NO_DOCUMENTATION 0)
ENDIF()

SET(PLUS_COMMIT_DATE ${PLUSAPP_COMMIT_DATE})
SET(PLUS_COMMIT_DATE_NO_DASHES ${PLUSAPP_COMMIT_DATE_NO_DASHES})
IF("${PLUSLIB_COMMIT_DATE}" STRGREATER "${PLUSAPP_COMMIT_DATE}")
  SET(PLUS_COMMIT_DATE ${PLUSLIB_COMMIT_DATE})
  SET(PLUS_COMMIT_DATE_NO_DASHES ${PLUSLIB_COMMIT_DATE_NO_DASHES})
ENDIF()

# One archive format that needs no tooling, plus the native installer for the
# platform. macOS previously fell through to a bare ZIP.
IF(WIN32)
  SET(CPACK_GENERATOR "ZIP" "NSIS")
ELSEIF(APPLE)
  SET(CPACK_GENERATOR "TGZ" "DragNDrop")
ELSE()
  SET(CPACK_GENERATOR "TGZ" "DEB")
ENDIF()

# Adds one entry to CPACK_INSTALL_CMAKE_PROJECTS, provided the named directory
# really is a build tree. Eight copies of this block used to be written out.
FUNCTION(plus_cpack_add_project _dir _project _component)
  IF(EXISTS "${_dir}/CMakeCache.txt")
    LIST(APPEND CPACK_INSTALL_CMAKE_PROJECTS "${_dir};${_project};${_component};/")
    SET(CPACK_INSTALL_CMAKE_PROJECTS "${CPACK_INSTALL_CMAKE_PROJECTS}" PARENT_SCOPE)
  ELSE()
    MESSAGE(WARNING "${_project} was not found at ${_dir}, so it will not be in the package.")
  ENDIF()
ENDFUNCTION()
SET(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Plus(Public software Library for UltraSound) for ${PLUSAPP_PACKAGE_EDITION_PLATFORM}")
SET(CPACK_PACKAGE_VENDOR "PerkLab, Queen's University")
# SET(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/ReadMe.txt")
IF(EXISTS ${PLUSLIB_SOURCE_DIR}/License.txt)
  SET(_license_file ${PLUSLIB_SOURCE_DIR}/License.txt)
ELSE()
  SET(_license_file ${PLUSLIB_SOURCE_DIR}/src/License.txt)
ENDIF()
SET(CPACK_RESOURCE_FILE_LICENSE "${_license_file}")
SET(CPACK_PACKAGE_VERSION_MAJOR ${PLUSAPP_VERSION_MAJOR})
SET(CPACK_PACKAGE_VERSION_MINOR ${PLUSAPP_VERSION_MINOR})
SET(CPACK_PACKAGE_VERSION_PATCH ${PLUSAPP_VERSION_PATCH})
SET(CPACK_PACKAGE_FILE_NAME "PlusApp-${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}.${PLUS_COMMIT_DATE_NO_DASHES}-${PLUSAPP_PACKAGE_EDITION_PLATFORM}" )
SET(CPACK_PACKAGE_INSTALL_DIRECTORY "${CPACK_PACKAGE_FILE_NAME}")
SET(CPACK_INSTALL_CMAKE_PROJECTS "${PlusApp_BINARY_DIR};PlusApp;ALL;/")
SET(CPACK_PACKAGE_EXECUTABLES
  "PlusServerLauncher" "Plus Server Launcher"
  )
IF (PLUSAPP_BUILD_fCal)
  LIST(APPEND CPACK_PACKAGE_EXECUTABLES "fCal" "Free-hand calibration(fCal)")
ENDIF()

IF(WIN32)
  SET(CPACK_NSIS_PACKAGE_NAME "Plus Applications ${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}.${PLUS_COMMIT_DATE} (${PLUSAPP_PACKAGE_EDITION_PLATFORM})" )

  # Install into c:\Users\<username>\PlusApp_...
  SET(CPACK_NSIS_INSTALL_ROOT "$PROFILE")

  # Do not ask for admin access rights(no UAC dialog), to allow installation without admin access
  SET(CPACK_NSIS_DEFINES ${CPACK_NSIS_DEFINES} "RequestExecutionLevel user")

  SET(CPACK_NSIS_EXTRA_INSTALL_COMMANDS)
  SET(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS)

  # Windows users may not be familiar how to open a command prompt, so create a shortcut for that
  LIST(APPEND CPACK_NSIS_EXTRA_INSTALL_COMMANDS "
    CreateShortCut \\\"$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\Plus command prompt.lnk\\\" \\\"$INSTDIR\\\\bin\\\\StartPlusCommandPrompt.bat\\\" \\\"$INSTDIR\\\\bin\\\\StartPlusCommandPrompt.bat\\\" \\\"$INSTDIR\\\\bin\\\\StartPlusCommandPrompt.ico\\\"
    ")
  LIST(APPEND CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS "
    !insertmacro MUI_STARTMENU_GETFOLDER Application $MUI_TEMP
    Delete \\\"$SMPROGRAMS\\\\$MUI_TEMP\\\\Plus command prompt.lnk\\\"
    ")
  IF(BUILD_DOCUMENTATION)
    # Create a shortcut to documentation as well
    LIST(APPEND CPACK_NSIS_EXTRA_INSTALL_COMMANDS "
      CreateShortCut \\\"$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\Plus user manual.lnk\\\" \\\"$INSTDIR\\\\doc\\\\PlusApp-UserManual.chm\\\"
      ")
    LIST(APPEND CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS "
      !insertmacro MUI_STARTMENU_GETFOLDER Application $MUI_TEMP
      Delete \\\"$SMPROGRAMS\\\\$MUI_TEMP\\\\Plus user manual.lnk\\\"
      ")
  ENDIF()
ELSEIF(UNIX AND NOT APPLE)
  SET(CPACK_DEBIAN_PACKAGE_MAINTAINER "Laboratory for Percutaneous Surgery")
  # Debian package names may only contain lower case letters, digits and
  # "+-." (https://www.debian.org/doc/debian-policy/ch-controlfields.html).
  STRING(TOLOWER "plusapp-${PLUSAPP_PACKAGE_EDITION_PLATFORM}" _debian_package_name)
  STRING(REGEX REPLACE "[^a-z0-9+.-]" "-" _debian_package_name "${_debian_package_name}")
  SET(CPACK_DEBIAN_PACKAGE_NAME "${_debian_package_name}")
  SET(CPACK_DEBIAN_PACKAGE_VERSION "${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}")
  SET(CPACK_DEBIAN_PACKAGE_RELEASE "${PLUSAPP_SHORT_REVISION}")
  # Ask dpkg what this machine is rather than declaring every package amd64.
  FIND_PROGRAM(DPKG_EXECUTABLE dpkg)
  IF(DPKG_EXECUTABLE)
    EXECUTE_PROCESS(COMMAND "${DPKG_EXECUTABLE}" --print-architecture
      OUTPUT_VARIABLE CPACK_DEBIAN_PACKAGE_ARCHITECTURE
      OUTPUT_STRIP_TRAILING_WHITESPACE
      )
  ENDIF()
  IF(NOT CPACK_DEBIAN_PACKAGE_ARCHITECTURE)
    IF(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
      SET(CPACK_DEBIAN_PACKAGE_ARCHITECTURE arm64)
    ELSEIF(CMAKE_SYSTEM_PROCESSOR MATCHES "^(i.86)$")
      SET(CPACK_DEBIAN_PACKAGE_ARCHITECTURE i386)
    ELSE()
      SET(CPACK_DEBIAN_PACKAGE_ARCHITECTURE amd64)
    ENDIF()
  ENDIF()

  # The package carries its own copies of VTK, ITK and Qt, so it depends only
  # on the system libraries deliberately left out of it.
  SET(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
  SET(CPACK_DEBIAN_PACKAGE_DEPENDS "libgl1, libxcb-xinerama0, libxkbcommon-x11-0, libxcb-icccm4, libxcb-image0, libxcb-keysyms1, libxcb-render-util0, libfontconfig1, libdbus-1-3")
  SET(CPACK_DEBIAN_COMPRESSION_TYPE gzip)
  SET(CPACK_DEBIAN_PACKAGE_SECTION science)
  SET(CPACK_DEBIAN_PACKAGE_PRIORITY optional)
  SET(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://plustoolkit.github.io/")
ENDIF()

plus_cpack_add_project("${PlusLib_DIR}" PlusLib RuntimeExecutables)
plus_cpack_add_project("${PlusLib_DIR}" PlusLib RuntimeLibraries)
plus_cpack_add_project("${PlusLib_DIR}" PlusLib Scripts)
IF(PLUS_USE_TextRecognizer)
  plus_cpack_add_project("${PlusLib_DIR}" PlusLib LanguageData)
ENDIF()

# Third-party runtime libraries.
#
# On Windows each dependency is packaged from its own install rules, and Qt is
# collected by windeployqt. Everywhere else the closure is computed from the
# built executables by plus_install_runtime_dependencies(), which does not
# need any of these directories to be named, and which also picks up the
# libraries these projects have no install rules for.
SET(CMAKE_INSTALL_OPENMP_LIBRARIES ${PLUS_USE_OpenCV})

IF(WIN32)
  # VTK_DIR and ITK_DIR are <prefix>/lib/cmake/<name>[-x.y]; three levels up is
  # the install prefix. Verified by looking for the directory the config file
  # was found in, so a build tree (which has no such layout) is not mistaken
  # for an install tree.
  FOREACH(_third_party VTK ITK)
    GET_FILENAME_COMPONENT(_prefix "${${_third_party}_DIR}/../../.." ABSOLUTE)
    IF(IS_DIRECTORY "${_prefix}/lib/cmake" AND IS_DIRECTORY "${_prefix}/bin")
      INSTALL(DIRECTORY "${_prefix}/bin/"
        DESTINATION ${PLUSAPP_INSTALL_BIN_DIR}
        COMPONENT RuntimeLibraries
        FILES_MATCHING PATTERN "*.dll"
        )
    ELSE()
      MESSAGE(WARNING "${_third_party} was not installed, so its libraries will not be in the package. Build with PLUSBUILD_INSTALL_${_third_party} turned on.")
    ENDIF()
  ENDFOREACH()

  IF(PLUS_USE_OpenIGTLink)
    plus_cpack_add_project("${OpenIGTLink_DIR}" OpenIGTLink RuntimeLibraries)
    plus_cpack_add_project("${OpenIGTLinkIO_DIR}" OpenIGTLinkIO RuntimeLibraries)
  ENDIF()
  IF(PLUS_USE_OpenCV OR PLUS_USE_OvrvisionPro)
    plus_cpack_add_project("${OpenCV_DIR}" OpenCV libs)
  ENDIF()
  IF(PLUS_USE_OvrvisionPro)
    plus_cpack_add_project("${OvrvisionPro_DIR}" OvrvisionPro RuntimeLibraries)
  ENDIF()
  IF(PLUS_USE_aruco)
    plus_cpack_add_project("${aruco_DIR}" aruco ALL)
  ENDIF()
  plus_cpack_add_project("${vtkAddon_DIR}" vtkAddon RuntimeLibraries)
  plus_cpack_add_project("${IGSIO_DIR}" IGSIO RuntimeLibraries)
ENDIF()


# Settings that differ between the generators are decided per package, while
# CPack is running, rather than here where only one value can be given.
CONFIGURE_FILE(
  "${CMAKE_CURRENT_SOURCE_DIR}/CPackProjectConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/CPackProjectConfig.cmake"
  @ONLY
  )
SET(CPACK_PROJECT_CONFIG_FILE "${CMAKE_CURRENT_BINARY_DIR}/CPackProjectConfig.cmake")

INCLUDE(InstallRequiredSystemLibraries)
INCLUDE(CPack)
