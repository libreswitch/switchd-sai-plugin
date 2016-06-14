/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#ifndef SAI_VENDOR_HOST_INTF_H
#define SAI_VENDOR_HOST_INTF_H 1

#include <sai-common.h>

DECLARE_VENDOR_CLASS_GETTER(struct host_intf_class, host_intf);

#define ops_sai_host_intf_class()     (CLASS_VENDOR_GETTER(host_intf)())

#endif /* sai-vendor-host-intf.h */
