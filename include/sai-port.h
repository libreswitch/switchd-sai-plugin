/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#ifndef SAI_PORT_H
#define SAI_PORT_H 1

/* We can't get default pvid from openswitch, but according to their convention
 * it is equal to 1.
 */
#define OPS_SAI_PORT_DEFAULT_PVID 1

struct ops_sai_port_config {
    bool hw_enable;
    bool autoneg;
    bool full_duplex;
    bool pause_tx;
    bool pause_rx;
    int mtu;
    int speed;
};

enum netdev_flags;

int ops_sai_port_config_get(uint32_t, struct ops_sai_port_config *);
int ops_sai_port_config_set(uint32_t, const struct ops_sai_port_config *,
                            struct ops_sai_port_config *);
int ops_sai_port_mtu_get(uint32_t, int *);
int ops_sai_port_mtu_set(uint32_t, int);
int ops_sai_port_carrier_get(uint32_t, bool *);
int ops_sai_port_flags_update(uint32_t, enum netdev_flags, enum netdev_flags,
                              enum netdev_flags *);
int ops_sai_port_pvid_get(uint32_t, sai_vlan_id_t *);
int ops_sai_port_pvid_set(uint32_t, sai_vlan_id_t);
int ops_sai_port_stats_get(uint32_t, struct netdev_stats *);

#endif /* sai-port.h */
