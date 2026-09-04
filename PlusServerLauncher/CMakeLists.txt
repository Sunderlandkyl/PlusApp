PROJECT(PlusServerLauncher)

# --------------------------------------------------------------------------
# Sources
SET(PlusServerLauncher_SRCS
  PlusServerLauncherMain.cxx
  PlusServerLauncherMainWindow.cxx
  )

IF(WIN32)
  SET(PlusServerLauncher_SRCS
    ${PlusServerLauncher_SRCS}
    PlusServerLauncher.rc
    )
ENDIF()

SET(PlusServerLauncher_UI_HDRS
  PlusServerLauncherMainWindow.h
  )

SET(PlusServerLauncher_UI_SRCS
  PlusServerLauncherMainWindow.ui 
  )

SET(PlusServerLauncher_QT_Resources
  PlusServerLauncher.qrc
  ${PLUSLIB_WIDGETS_QRC}
  )

# --------------------------------------------------------------------------
# Build the library
SET (PlusServerLauncher_LIBS 
  Qt5::Widgets
  Qt5::Network
  vtkPlusCommon
  PlusWidgets
  vtkPlusServer
  )
IF(TARGET vtkRenderingGL2PS${VTK_RENDERING_BACKEND})
  LIST(APPEND PlusServerLauncher_LIBS
    vtkRenderingGL2PS${VTK_RENDERING_BACKEND}
    )
ELSEIF(TARGET vtkRenderingGL2PS)
  LIST(APPEND PlusServerLauncher_LIBS
    vtkRenderingGL2PS
    )
ENDIF()

IF(TARGET igtlioLogic)
  LIST(APPEND PlusServerLauncher_LIBS igtlioLogic)
ENDIF()

# WIN32 asks for a windowed application with no console attached. macOS gets a
# plain executable rather than a .app: the package is a directory of tools,
# with the libraries in a sibling lib/ directory that every executable reaches
# through "@executable_path/../lib". A bundle puts its executable two levels
# further down, where that path resolves to nothing, and macOS translocates an
# unsigned bundle launched from a disk image into a temporary directory of its
# own, so a bundle would have to carry a private copy of Qt, VTK and ITK to
# work at all.
SET(APP_MODE)
IF(WIN32)
  SET(APP_MODE WIN32)
ENDIF()

AddPlusQt5Executable( PlusServerLauncher ${APP_MODE} 
  ${PlusServerLauncher_SRCS} 
  ${PlusServerLauncher_UI_HDRS} 
  ${PlusServerLauncher_UI_SRCS} 
  ${PlusServerLauncher_QT_Resources}
  )
# The Qt compile definitions arrive through the Qt5:: imported targets that
# this executable links, so there is nothing to set here. The property was
# previously spelled COMPILE_DEFINTIIONS, which is not a property at all, so
# it had never taken effect.
target_include_directories(PlusServerLauncher PRIVATE 
  ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}
  )
TARGET_LINK_LIBRARIES( PlusServerLauncher PRIVATE ${PlusServerLauncher_LIBS} )

# --------------------------------------------------------------------------
# Install
IF(PLUSAPP_INSTALL_BIN_DIR)
  INSTALL(TARGETS PlusServerLauncher 
    DESTINATION ${PLUSAPP_INSTALL_BIN_DIR}
    COMPONENT RuntimeExecutables
    )
ENDIF()