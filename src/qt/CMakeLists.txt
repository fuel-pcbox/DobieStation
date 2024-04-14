set(TARGET DobieQt)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(CMAKE_VERSION VERSION_LESS "3.7.0")
    set(CMAKE_INCLUDE_CURRENT_DIR ON)
endif()

find_package(Qt6 COMPONENTS Core Widgets Gui REQUIRED)

set(SOURCES
    emuthread.cpp
    emuwindow.cpp
    settingswindow.cpp
    renderwidget.cpp
    gamelistwidget.cpp
    memcardwindow.cpp
    main.cpp
    settings.cpp
    bios.cpp)

set(HEADERS
    emuthread.hpp
    emuwindow.hpp
    settingswindow.hpp
    renderwidget.hpp
    gamelistwidget.hpp
    memcardwindow.hpp
    settings.hpp
    bios.hpp)

set(UIS
    memcardwindow.ui
)

add_executable(${TARGET} ${SOURCES} ${HEADERS})
set_target_properties(${TARGET} PROPERTIES OUTPUT_NAME "DobieStation") # Output as "DobieStation" instead of "DobieQt"

target_compile_features(${TARGET} PRIVATE cxx_std_20)

if (MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Zc:__cplusplus")
endif()

dobie_cxx_compile_options(${TARGET})
target_include_directories(${TARGET} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${Qt6Gui_PRIVATE_INCLUDE_DIRS})
target_link_libraries(${TARGET} Dobie::Core Qt::Core Qt::Widgets Qt::Gui)


install(TARGETS DobieQt RUNTIME DESTINATION bin)
