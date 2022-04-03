cmake_minimum_required(VERSION 3.1.0)
cmake_policy(SET CMP0042 NEW)
project(ModLibrary VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(CMAKE_VERSION VERSION_LESS "3.7.0")
    set(CMAKE_INCLUDE_CURRENT_DIR ON)
endif()

find_package(PkgConfig)
find_package(Qt5 REQUIRED COMPONENTS Core Widgets Multimedia Sql Gui)

add_executable(ModLibrary
    main.cpp
    modlibrary.ui
    modlibrary.cpp
    modlibrary.h
    modlibrary.qrc
    modinfo.cpp
    modinfo.h
    modinfo.ui
    settings.cpp
    settings.h
    settings.ui
    about.cpp
    about.h
    about.ui
    audioplayer.h
    base64.cpp
    base64.h
    database.cpp
    database.h
    qcheckboxex.h
    tablemodel.h
)

target_link_libraries(ModLibrary Qt5::Core)
target_link_libraries(ModLibrary Qt5::Widgets)
target_link_libraries(ModLibrary Qt5::Multimedia)
target_link_libraries(ModLibrary Qt5::Sql)
target_link_libraries(ModLibrary Qt5::Gui)

pkg_check_modules(OPENMPT REQUIRED libopenmpt)
include_directories(${OPENMPT_INCLUDE_DIRS})
target_link_libraries(ModLibrary ${OPENMPT_LIBRARIES})

pkg_check_modules(CHROMAPRINT REQUIRED libchromaprint)
include_directories(${CHROMAPRINT_INCLUDE_DIRS})
target_link_libraries(ModLibrary ${CHROMAPRINT_LIBRARIES})

pkg_check_modules(PORTAUDIO REQUIRED portaudiocpp)
include_directories(${PORTAUDIO_INCLUDE_DIRS})
target_link_libraries(ModLibrary ${PORTAUDIO_LIBRARIES})