cmake_minimum_required(VERSION 3.16)

# Credentials live in credentials.defaults, which is gitignored. Anything set
# there overrides sdkconfig.defaults, so secrets stay out of the repository.
set(SDKCONFIG_DEFAULTS "sdkconfig.defaults")
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/credentials.defaults")
    list(APPEND SDKCONFIG_DEFAULTS "credentials.defaults")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(piano-esp-mqtt)
