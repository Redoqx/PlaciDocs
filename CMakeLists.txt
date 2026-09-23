cmake_minimum_required(VERSION 3.20)
project(PlaciDocs VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(PLACI_BUILD_TESTS "Build unit tests" ON)
include(GNUInstallDirs)
if(PLACI_BUILD_TESTS)
  enable_testing()
endif()

# ---------------------------------------------------------------- third party
add_library(md4c STATIC third_party/md4c/md4c.c)
target_include_directories(md4c PUBLIC third_party/md4c)

# rapidyaml is vendored as a single header; its implementation is compiled
# once, in src/core/style/ryml_impl.cpp.
add_library(ryml INTERFACE)
target_include_directories(ryml SYSTEM INTERFACE third_party/ryml)
target_compile_definitions(ryml INTERFACE RYML_DEFAULT_CALLBACK_USES_EXCEPTIONS)

add_library(cli11 INTERFACE)
target_include_directories(cli11 SYSTEM INTERFACE third_party/cli11)

add_library(doctest INTERFACE)
target_include_directories(doctest SYSTEM INTERFACE third_party/doctest)

# ---------------------------------------------------------------- libplaci
add_library(placi STATIC
  src/core/ast.cpp
  src/core/util.cpp
  src/core/parser/preprocess.cpp
  src/core/parser/md_parser.cpp
  src/core/passes/passes.cpp
  src/core/style/ryml_impl.cpp
  src/core/style/style_loader.cpp
  src/core/rules/rule_engine.cpp
  src/core/latex/escape.cpp
  src/core/latex/preamble.cpp
  src/core/latex/emitter.cpp
  src/core/compile/process.cpp
  src/core/compile/tectonic.cpp
  src/core/pipeline.cpp
)
target_include_directories(placi PUBLIC src)
target_link_libraries(placi PUBLIC md4c ryml)

if(MSVC)
  target_compile_options(placi PRIVATE /W4 /utf-8)
else()
  target_compile_options(placi PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------- CLI
add_executable(placi_cli src/cli/main.cpp)
set_target_properties(placi_cli PROPERTIES OUTPUT_NAME placi)
target_link_libraries(placi_cli PRIVATE placi cli11)
if(MINGW)
  target_link_options(placi_cli PRIVATE -static)
endif()

# Put the bundled Tectonic binary and built-in styles next to the executable,
# which is where the runtime looks for them.
set(PLACI_TECTONIC "${CMAKE_SOURCE_DIR}/third_party/tectonic/tectonic${CMAKE_EXECUTABLE_SUFFIX}")
add_custom_command(TARGET placi_cli POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/styles $<TARGET_FILE_DIR:placi_cli>/styles
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PLACI_TECTONIC} $<TARGET_FILE_DIR:placi_cli>/
  VERBATIM)

# ---------------------------------------------------------------- GUI (Qt 6)
# Configure with the compiler Qt was built with, e.g.
#   cmake -S . -B build-qt -G Ninja -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/mingw_64
#         -DCMAKE_CXX_COMPILER=C:/Qt/Tools/mingw1310_64/bin/g++.exe ...
option(PLACI_BUILD_GUI "Build the Qt desktop app" ON)
if(PLACI_BUILD_GUI)
  find_package(Qt6 6.5 QUIET COMPONENTS Widgets PdfWidgets Concurrent)
endif()
if(PLACI_BUILD_GUI AND Qt6_FOUND)
  qt_standard_project_setup()
  qt_add_executable(placidocs WIN32
    src/gui/main.cpp
    src/gui/MainWindow.cpp
    src/gui/Editor.cpp
    src/gui/Highlighter.cpp
    src/gui/BuildController.cpp
    src/gui/Dialogs.cpp
    src/gui/DocumentText.cpp
    src/gui/Bibliography.cpp
  )
  target_link_libraries(placidocs PRIVATE placi Qt6::Widgets Qt6::PdfWidgets Qt6::Concurrent)
  target_include_directories(placidocs PRIVATE src)
  add_custom_command(TARGET placidocs POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/styles $<TARGET_FILE_DIR:placidocs>/styles
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PLACI_TECTONIC} $<TARGET_FILE_DIR:placidocs>/
    VERBATIM)
  # Copy the Qt runtime next to the app so it starts from the build directory.
  find_program(PLACI_WINDEPLOYQT windeployqt HINTS "${Qt6_DIR}/../../../bin")
  if(WIN32 AND PLACI_WINDEPLOYQT)
    # Qt plugins go to qtplugins/ so they do not mix with PlaciDocs' styles/ folder.
    file(WRITE ${CMAKE_BINARY_DIR}/qt.conf "[Paths]\nPlugins = qtplugins\n")
    add_custom_command(TARGET placidocs POST_BUILD
      COMMAND ${PLACI_WINDEPLOYQT} --no-translations --no-system-d3d-compiler --no-opengl-sw
              --plugindir $<TARGET_FILE_DIR:placidocs>/qtplugins $<TARGET_FILE:placidocs>
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_BINARY_DIR}/qt.conf $<TARGET_FILE_DIR:placidocs>/qt.conf
      VERBATIM)
  endif()
  install(TARGETS placidocs RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

  if(PLACI_BUILD_TESTS)
    # The document edits the GUI performs are plain string logic: test them here.
    qt_add_executable(placi_gui_tests tests/main.cpp tests/test_gui_text.cpp tests/test_gui_dialogs.cpp
                      src/gui/DocumentText.cpp src/gui/Bibliography.cpp src/gui/Dialogs.cpp)
    target_link_libraries(placi_gui_tests PRIVATE Qt6::Widgets doctest)
    target_include_directories(placi_gui_tests PRIVATE src)
    # The tests run headless, so they need Qt's own plugin folder (the deployed
    # one next to the app carries only the Windows platform plugin).
    get_target_property(_qt_core_loc Qt6::Core LOCATION)
    get_filename_component(_qt_bin "${_qt_core_loc}" DIRECTORY)
    target_compile_definitions(placi_gui_tests PRIVATE PLACI_QT_PLUGINS="${_qt_bin}/../plugins")
    add_test(NAME placi_gui_tests COMMAND placi_gui_tests)
  endif()
elseif(PLACI_BUILD_GUI)
  message(STATUS "Qt 6 (Widgets, PdfWidgets) not found: the desktop app is not built")
endif()

# ---------------------------------------------------------------- bundled LaTeX packages
# `cmake --build build --target texcache` builds a coverage document with every
# coverage style into texcache/ next to the executable. placi then runs Tectonic
# against that cache with --only-cached, so end users never need the network.
set(PLACI_TEXCACHE_DIR "$<TARGET_FILE_DIR:placi_cli>/texcache")
set(PLACI_COVERAGE "${CMAKE_SOURCE_DIR}/tests/texcache")
set(_texcache_cmds)
foreach(style skripsi-umum laporan-kantor default cov-english cov-malay cov-extra cov-ieee cov-plain)
  if(style MATCHES "^cov-")
    set(_style_arg "${PLACI_COVERAGE}/${style}.yaml")
  else()
    set(_style_arg "${style}")
  endif()
  list(APPEND _texcache_cmds
    COMMAND ${CMAKE_COMMAND} -E env PLACI_TEXCACHE=${PLACI_TEXCACHE_DIR}
            $<TARGET_FILE:placi_cli> build ${PLACI_COVERAGE}/coverage.md -s ${_style_arg} --online
            -o ${CMAKE_BINARY_DIR}/texcache-${style}.pdf)
endforeach()
list(APPEND _texcache_cmds
  COMMAND ${CMAKE_COMMAND} -E env PLACI_TEXCACHE=${PLACI_TEXCACHE_DIR}
          $<TARGET_FILE:placi_cli> build ${CMAKE_SOURCE_DIR}/examples/skripsi/main.md --online
          -o ${CMAKE_BINARY_DIR}/texcache-example.pdf)
add_custom_target(texcache
  COMMAND ${CMAKE_COMMAND} -E make_directory ${PLACI_TEXCACHE_DIR}
  ${_texcache_cmds}
  DEPENDS placi_cli
  COMMENT "Downloading the LaTeX packages PlaciDocs uses into texcache/"
  VERBATIM)

# ---------------------------------------------------------------- install & package
# Everything an end user needs lands in one folder: both apps, the LaTeX engine,
# its package cache, the built-in styles and (for the GUI) the Qt runtime.
install(TARGETS placi_cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(PROGRAMS ${PLACI_TECTONIC} DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY styles DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY ${CMAKE_BINARY_DIR}/texcache DESTINATION ${CMAKE_INSTALL_BINDIR} OPTIONAL)
install(FILES README.md LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(DIRECTORY examples DESTINATION ${CMAKE_INSTALL_DOCDIR})

if(WIN32 AND TARGET placidocs)
  # windeployqt already placed the Qt runtime next to the executable.
  install(CODE "
    file(GLOB _placi_dlls \"${CMAKE_BINARY_DIR}/*.dll\")
    if(_placi_dlls)
      file(INSTALL DESTINATION \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}\" TYPE SHARED_LIBRARY FILES \${_placi_dlls})
    endif()")
  install(DIRECTORY ${CMAKE_BINARY_DIR}/qtplugins DESTINATION ${CMAKE_INSTALL_BINDIR} OPTIONAL)
  install(FILES ${CMAKE_BINARY_DIR}/qt.conf DESTINATION ${CMAKE_INSTALL_BINDIR} OPTIONAL)
endif()

set(CPACK_PACKAGE_NAME PlaciDocs)
set(CPACK_PACKAGE_VENDOR PlaciDocs)
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Tulis Markdown, dapatkan PDF sesuai aturan penulisan instansi")
set(CPACK_PACKAGE_INSTALL_DIRECTORY PlaciDocs)
set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_SOURCE_DIR}/LICENSE)
set(CPACK_PACKAGE_EXECUTABLES placidocs "PlaciDocs")
set(CPACK_GENERATOR ZIP)
include(CPack)

# ---------------------------------------------------------------- tests
if(PLACI_BUILD_TESTS)
  add_executable(placi_tests
    tests/main.cpp
    tests/test_parser.cpp
    tests/test_style.cpp
    tests/test_rules.cpp
    tests/test_latex.cpp
  )
  target_link_libraries(placi_tests PRIVATE placi doctest)
  target_compile_definitions(placi_tests PRIVATE PLACI_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  if(MINGW)
    target_link_options(placi_tests PRIVATE -static)
  endif()
  add_test(NAME placi_tests COMMAND placi_tests)
endif()
