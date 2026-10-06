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
endif()
