/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#ifndef SAI_VLAN_H
#define SAI_VLAN_H 1

#include <sai.h>

int ops_sai_vlan_access_port_add(sai_vlan_id_t, uint32_t);
int ops_sai_vlan_access_port_del(sai_vlan_id_t, uint32_t);
int ops_sai_vlan_trunks_port_add(const unsigned long *, uint32_t);
int ops_sai_vlan_trunks_port_del(const unsigned long *, uint32_t);
int ops_sai_vlan_set(int, bool);

#endif /* sai-vlan.h */
