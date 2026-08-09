# Phototherapy_Timer — ESP-IDF app: session_timer
cmake_minimum_required(VERSION 3.16)

# Shared components (uh_ota, …) live under esp32_firmware/components/
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(session_timer)
