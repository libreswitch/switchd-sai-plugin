/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#ifndef SAI_VENDOR_NEIGHBOR_H
#define SAI_VENDOR_NEIGHBOR_H 1

#include <sai-common.h>

DECLARE_VENDOR_CLASS_GETTER(struct neighbor_class, neighbor);

#define ops_sai_neighbor_class()     (CLASS_VENDOR_GETTER(neighbor)())

#endif /* sai-vendor-neighbor.h */
