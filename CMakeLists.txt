cmake_minimum_required(VERSION 3.10)
project(SSFHelper)

set(CMAKE_CXX_STANDARD 11)

set(SOURCE_FILES main.cpp)

# Enable resource compiler for Windows and add resources if they exist
if(WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources.rc")
    enable_language(RC)
    set(RESOURCE_FILES resources.rc)
    add_executable(SSFHelper WIN32 ${SOURCE_FILES} ${RESOURCE_FILES})
else()
    add_executable(SSFHelper WIN32 ${SOURCE_FILES})
endif()

# Link against comdlg32 for file dialog
target_link_libraries(SSFHelper comdlg32)

# Static linking for standalone executable
set_target_properties(SSFHelper PROPERTIES 
    COMPILE_FLAGS "-m32" 
    LINK_FLAGS "-m32 -mwindows -static-libgcc -static-libstdc++ -static"
)
