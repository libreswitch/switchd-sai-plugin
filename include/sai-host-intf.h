/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#ifndef SAI_HOST_INTF_H
#define SAI_HOST_INTF_H 1

int ops_sai_hostint_netdev_create(const char *, uint32_t);
void ops_sai_hostint_traps_register(void);

#endif /* sai-host-intf.h */
