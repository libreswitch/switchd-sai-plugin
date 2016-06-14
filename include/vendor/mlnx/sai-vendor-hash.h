/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#ifndef SAI_VENDOR_HASH_H
#define SAI_VENDOR_HASH_H 1

#include <sai-common.h>

DECLARE_VENDOR_CLASS_GETTER(struct hash_class, hash);

#define ops_sai_hash_class() (CLASS_VENDOR_GETTER(hash)())

#endif /* sai-vendor-hash.h */
