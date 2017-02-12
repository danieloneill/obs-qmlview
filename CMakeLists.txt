set(CMAKE_INCLUDE_CURRENT_DIR TRUE)
set(CMAKE_AUTOMOC TRUE)

find_package(Qt5Quick REQUIRED)
if(NOT Qt5Quick_FOUND)
        if (ENABLE_UI)
                message(FATAL_ERROR "Failed to find Qt5 quickness")
        else()
                message(STATUS "Qt5 quickness not found - Qmlview disabled")
                return()
        endif()
endif()

find_package(Qt5Widgets REQUIRED)
if(NOT Qt5Widgets_FOUND)
        if (ENABLE_UI)
                message(FATAL_ERROR "Failed to find Qt5 midgets")
        else()
                message(STATUS "Qt5 midgets not found - Qmlview disabled")
                return()
        endif()
endif()

find_package(Qt5QuickWidgets REQUIRED)
if(NOT Qt5QuickWidgets_FOUND)
        if (ENABLE_UI)
                message(FATAL_ERROR "Failed to find Qt5 quickmidgets")
        else()
                message(STATUS "Qt5 quickmidgets not found - Qmlview disabled")
                return()
        endif()
endif()

find_package(Qt5WebEngine REQUIRED)
if(NOT Qt5WebEngine_FOUND)
        if (ENABLE_UI)
                message(FATAL_ERROR "Failed to find Qt5 quickmidgets")
        else()
                message(STATUS "Qt5 quickmidgets not found - Qmlview disabled")
                return()
        endif()
endif()

set(qmlview_SOURCES
    qmlview.cpp
)

set( qmlview_HEADERS
    qmlview.h
)

# qt5_wrap_headers( ${qmlview_HEADERS} );

add_library(obs-qmlview MODULE
        ${qmlview_SOURCES}
        ${qmlview_HEADERS}
)
target_link_libraries(obs-qmlview
        libobs
        Qt5::Quick
        Qt5::QuickWidgets
        Qt5::Widgets
        Qt5::WebEngine
)
install_obs_plugin_with_data(obs-qmlview data)
