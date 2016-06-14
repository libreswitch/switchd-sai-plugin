/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#ifndef SAI_VENDOR_ROUTE_H
#define SAI_VENDOR_ROUTE_H 1

#include <sai-common.h>

DECLARE_VENDOR_CLASS_GETTER(struct route_class, route);

#define ops_sai_route_class()     (CLASS_VENDOR_GETTER(route)())

#endif /* sai-vendor-route.h */
