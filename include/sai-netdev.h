/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.  
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#ifndef NETDEV_SAI_H
#define NETDEV_SAI_H 1

#include <saitypes.h>
#include <netdev-provider.h>

void netdev_sai_register(void);
sai_object_id_t netdev_sai_oid_get(struct netdev *);
void netdev_sai_port_oper_state_changed(sai_object_id_t, int);

#endif /* sai-netdev.h */
