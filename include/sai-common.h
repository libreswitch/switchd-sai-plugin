/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#ifndef SAI_COMMON_H
#define SAI_COMMON_H 1

#include <saitypes.h>
#include <netdev-provider.h>

#define PROVIDER_INIT_GENERIC(NAME, VALUE)       .NAME = VALUE,

#ifdef OPS
#define PROVIDER_INIT_OPS_SPECIFIC(NAME, VALUE)  .NAME = VALUE,
#else
#define PROVIDER_INIT_OPS_SPECIFIC(NAME, VALUE)
#endif

#endif /* SAI_COMMON_H */
