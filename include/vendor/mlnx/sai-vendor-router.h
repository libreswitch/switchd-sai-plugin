/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#ifndef SAI_VENDOR_ROUTER_H
#define SAI_VENDOR_ROUTER_H 1

#include <sai-common.h>

DECLARE_VENDOR_CLASS_GETTER(struct router_class, router);

#define ops_sai_router_class()     (CLASS_VENDOR_GETTER(router)())

#endif /* sai-vendor-router.h */
