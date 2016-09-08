/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.  
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#ifndef SAI_NETDEV_H
#define SAI_NETDEV_H 1

#include <saitypes.h>
#include <netdev-provider.h>
#include <sai-common.h>

void netdev_sai_register(void);
uint32_t netdev_sai_hw_id_get(struct netdev *);
void netdev_sai_port_oper_state_changed(sai_object_id_t, int);
void netdev_sai_port_lane_state_changed(sai_object_id_t, int);
int netdev_sai_set_router_intf_handle(struct netdev *, const handle_t *);
int netdev_sai_get_lane_state(struct netdev *, bool *);

#endif /* sai-netdev.h */
