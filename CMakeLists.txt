# Copyright Mellanox Technologies, Ltd. 2001-2016.  
# This software product is licensed under Apache version 2, as detailed in
# the COPYING file.

project (ovs_sai_plugin)
cmake_minimum_required (VERSION 2.8)
set (SRC_DIR src)
set (INCL_DIR include)

add_definitions(-DOPS)

# Define compile flags
# -g will should be removed in later versions
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu99 -Werror -Wall -g -Wno-error=unused-function")

###
### Define sources
###

set (SOURCES ${SRC_DIR}/sai-plugins.c
             ${SRC_DIR}/sai-netdev.c
             ${SRC_DIR}/sai-ofproto-provider.c
             ${SRC_DIR}/sai-api-class.c
    )

###
### Define and locate needed libraries and includes
###
include(FindPkgConfig)
pkg_check_modules(OVSCOMMON REQUIRED libovscommon)
pkg_check_modules(SAI REQUIRED sai)

include_directories(${CMAKE_SOURCE_DIR}/${INCL_DIR}
                    ${OVSCOMMON_INCLUDE_DIRS}
                    ${SAI_INCLUDE_DIRS}
    )

###
### Create our library
###

add_library (ovs_sai_plugin SHARED ${SOURCES})

target_link_libraries (ovs_sai_plugin openvswitch sai)

###
### Installation
###
install(TARGETS ovs_sai_plugin
        LIBRARY DESTINATION lib/openvswitch/plugins
    )
