cmake_minimum_required(VERSION 3.16)
project(lingmo-optimizer LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(GNUInstallDirs)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Concurrent DBus)

set(HELPER_DIR ${CMAKE_INSTALL_PREFIX}/lib/lingmo-optimizer)

# Pure parts (parsers, disk usage, safety policies): shared with the tests
add_library(optimizer-core STATIC
    src/core/procparse.cpp
    src/core/procparse.h
    src/core/diskusage.cpp
    src/core/diskusage.h
    src/core/policy.cpp
    src/core/policy.h
    src/core/format.cpp
    src/core/format.h
    src/core/hogs.cpp
    src/core/hogs.h
    src/core/devcaches.cpp
    src/core/devcaches.h
)
target_include_directories(optimizer-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(optimizer-core PUBLIC Qt6::Core)

add_executable(lingmo-optimizer
    src/main.cpp
    src/systeminfo.cpp
    src/systeminfo.h
    src/processmonitor.cpp
    src/processmonitor.h
    src/desktopapps.cpp
    src/desktopapps.h
    src/storageanalyzer.cpp
    src/storageanalyzer.h
    src/suggestions.cpp
    src/suggestions.h
    src/cleaner.cpp
    src/cleaner.h
    src/devtools.cpp
    src/devtools.h
    src/fileactions.cpp
    src/fileactions.h
    qml.qrc
)
target_compile_definitions(lingmo-optimizer PRIVATE HELPER_PATH="${HELPER_DIR}/clean-system")

target_link_libraries(lingmo-optimizer
  PRIVATE
  optimizer-core
  Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Concurrent Qt6::DBus
)

# Lingmo Intents: optimizer.analyze, optimizer.clean.caches on com.lingmo.Optimizer
# /Intents (`lingmo-optimizer --intents`). Optional: without the library the
# app builds the same, only without the actions.
find_package(LingmoIntents 1.0 QUIET)
if(LingmoIntents_FOUND)
    target_sources(lingmo-optimizer PRIVATE src/optimizerintents.cpp src/optimizerintents.h)
    target_compile_definitions(lingmo-optimizer PRIVATE HAVE_LINGMO_INTENTS)
    target_link_libraries(lingmo-optimizer PRIVATE LingmoIntents::LingmoIntents)
    lingmo_install_intents(MANIFEST data/intents/lingmo-optimizer.json SERVICE data/com.lingmo.Optimizer.service)
else()
    message(STATUS "LingmoIntents not found: Optimizer is built without app actions (intents)")
endif()

install(TARGETS lingmo-optimizer RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(PROGRAMS helper/clean-system DESTINATION ${HELPER_DIR})
install(FILES data/com.lingmo.optimizer.policy DESTINATION ${CMAKE_INSTALL_DATADIR}/polkit-1/actions)
install(FILES lingmo-optimizer.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

option(BUILD_TESTING "Build the unit tests" OFF)
if(BUILD_TESTING)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(tst_optimizer tests/tst_optimizer.cpp)
    target_link_libraries(tst_optimizer PRIVATE optimizer-core Qt6::Test)
    add_test(NAME tst_optimizer COMMAND tst_optimizer)
    add_executable(tst_devcaches tests/tst_devcaches.cpp)
    target_link_libraries(tst_devcaches PRIVATE optimizer-core Qt6::Test)
    add_test(NAME tst_devcaches COMMAND tst_devcaches)
endif()
