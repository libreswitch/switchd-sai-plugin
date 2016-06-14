# Copyright Mellanox Technologies, Ltd. 2001-2016.  
# This software product is licensed under Apache version 2, as detailed in
# the COPYING file.

project (ovs_sai_plugin)
cmake_minimum_required (VERSION 2.8)
set (SRC_DIR src)
set (INCL_DIR include)

# Define compile flags
# -g will should be removed in later versions
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu99 -Werror -Wall -g -Wno-error=unused-function")

###
### Define vendor
###

if(DEFINED SAI_VENDOR)
  add_definitions(-DSAI_VENDOR)
  add_definitions(-D${SAI_VENDOR}_SAI)
  string(TOLOWER ${SAI_VENDOR} SAI_VENDOR_DIR)
  set(SAI_VENDOR_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/${INCL_DIR}/vendor/${SAI_VENDOR_DIR})
  set(SAI_VENDOR_SOURCES ${SRC_DIR}/vendor/${SAI_VENDOR_DIR}/*.c)
endif(DEFINED SAI_VENDOR)

set(SAI_INIT_CONFIG_FILE_PATH " " CACHE STRING "path to SAI configuration file")

configure_file(${CMAKE_SOURCE_DIR}/${INCL_DIR}/sai-api-class.h.in
               ${CMAKE_SOURCE_DIR}/${INCL_DIR}/sai-api-class.h)

###
### Define sources
###

file(GLOB SOURCES ${SRC_DIR}/*.c ${SAI_VENDOR_SOURCES})

###
### Define and locate needed libraries and includes
###
include(FindPkgConfig)
pkg_check_modules(OVSCOMMON REQUIRED libovscommon)
pkg_check_modules(SAI REQUIRED sai)
pkg_check_modules(CONFIG_YAML REQUIRED ops-config-yaml)
if(SAI_VENDOR STREQUAL "MLNX")
# libnl is required by sxnet library. Should be moved to vendor specific code
PKG_SEARCH_MODULE(LIBNL libnl-3.0 libnl-3 libnl nl-3 nl)
endif()

include_directories(${CMAKE_SOURCE_DIR}/${INCL_DIR}
                    ${SAI_VENDOR_INCLUDE_DIR}
                    ${OVSCOMMON_INCLUDE_DIRS}
                    ${SAI_INCLUDE_DIRS}
    )


if(SAI_VENDOR STREQUAL "MLNX")
include_directories(${LIBNL_INCLUDE_DIRS})
endif()

###
### Create our library
###

add_library (ovs_sai_plugin SHARED ${SOURCES})

target_link_libraries (ovs_sai_plugin openvswitch sai config-yaml)

if(SAI_VENDOR STREQUAL "MLNX")
target_link_libraries (ovs_sai_plugin sxnet)
endif()

###
### Installation
###
install(TARGETS ovs_sai_plugin
        LIBRARY DESTINATION lib/openvswitch/plugins
    )
