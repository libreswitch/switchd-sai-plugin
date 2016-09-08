/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#ifndef SAI_PORT_H
#define SAI_PORT_H 1

#include <sai-common.h>
#ifdef SAI_VENDOR
#include <sai-vendor-common.h>
#endif /* SAI_VENDOR */

/* We can't get default pvid from openswitch, but according to their convention
 * it is equal to 1.
 */
#define OPS_SAI_PORT_DEFAULT_PVID 1

enum netdev_flags;
struct netdev_stats;

struct ops_sai_port_config {
    bool hw_enable;
    bool autoneg;
    bool full_duplex;
    bool pause_tx;
    bool pause_rx;
    int mtu;
    int speed;
    int max_speed;
};

struct split_info {
    bool disable_neighbor;
    uint32_t neighbor_hw_id;
};

enum ops_sai_port_transaction {
    OPS_SAI_PORT_TRANSACTION_TO_L2,
    OPS_SAI_PORT_TRANSACTION_TO_L3,
    __OPS_SAI_PORT_TRANSACTION_MAX,
    OPS_SAI_PORT_TRANSACTION_MIN = OPS_SAI_PORT_TRANSACTION_TO_L2,
    OPS_SAI_PORT_TRANSACTION_MAX = __OPS_SAI_PORT_TRANSACTION_MAX - 1
};

enum ops_sai_port_split {
    OPS_SAI_PORT_SPLIT_UNSPLIT,
    OPS_SAI_PORT_SPLIT_TO_2,
    OPS_SAI_PORT_SPLIT_TO_4,
    __OPS_SAI_PORT_SPLIT_MAX,
    OPS_SAI_PORT_SPLIT_MIN = OPS_SAI_PORT_SPLIT_UNSPLIT,
    OPS_SAI_PORT_SPLIT_MAX = __OPS_SAI_PORT_SPLIT_MAX - 1
};

struct port_class {
    /*
     * Initialize port functionality.
     */
    void (*init)(void);
    /*
     * Reads port configuration.
     *
     * @param[in] hw_id port HW lane id.
     * @param[out] conf pointer to port configuration.
     *
     * @return 0, sai status converted to errno otherwise.
     */
    int (*config_get)(uint32_t hw_id, struct ops_sai_port_config *conf);
    /*
     * Applies new port configuration.
     *
     * @param[in] hw_id port HW lane id.
     * @param[in] new pointer to new port configuration.
     * @param[out] old pointer to current port configuration, will be updated with
     * values from new configuration.
     *
     * @return 0, sai status converted to errno otherwise.
     */
    int (*config_set)(uint32_t hw_id, const struct ops_sai_port_config *new,
                      struct ops_sai_port_config *old);
    /*
     * Reads port mtu.
     *
     * @param[in] hw_id port HW lane id.
     * @param[out] mtu pointer to mtu variable, will be set to current value.
     *
     * @return 0, sai status converted to errno otherwise.
     */
    int (*mtu_get)(uint32_t hw_id, int *mtu);
    /*
     * Sets port mtu.
     *
     * @param[in] hw_id port HW lane id.
     * @param[in] mtu value to be applied.
     *
     * @return 0, sai status converted to errno otherwise.
     */
    int (*mtu_set)(uint32_t hw_id, int mtu);
    /*
     * Reads port operational state.
     *
     * @param[in] hw_id port HW lane id.
     * @param[out] carrier pointer to boolean, set to true if port is in operational
     * state.
     *
     * @return 0, sai status converted to errno otherwise.
     */
    int (*carrier_get)(uint32_t hw_id, bool *carrier);
    /*
     * Updates netdevice flags.
     *
     * @param[in] hw_id port HW lane id.
     * @param[in] off flags to be cleared.
     * @param[in] on flags to be set.
     * @param[out] old_flagsp set with current flags.
     *
     * @return 0, sai status converted to errno otherwise.
     */
    int (*flags_update)(uint32_t hw_id,
                        enum netdev_flags off,
                        enum netdev_flags on,
                        enum netdev_flags *old_flags);
    /*
     * Reads port VLAN ID.
     *
     * @param[in] hw_id port HW lane id.
     * @param[out] pvid pointer to pvid variable, will be set to current pvid.
     *
     * @return 0, sai status converted to errno otherwise.
     */
    int (*pvid_get)(uint32_t hw_id, sai_vlan_id_t *pvid);
    /*
     * Sets port VLAN ID.
     *
     * @param[in] hw_id port HW lane id.
     * @param[in] pvid new VLAN ID to be set.
     *
     * @return 0, sai status converted to errno otherwise.
     */
    int (*pvid_set)(uint32_t hw_id, sai_vlan_id_t pvid);
    /*
     * Get port statistics.
     *
     * @param[in] hw_id port HW lane id.
     * @param[out] stats pointer to netdev statistics.
     *
     * @return 0, sai status converted to errno otherwise.
     */
    int (*stats_get)(uint32_t hw_id, struct netdev_stats *stats);
    /*
     * Get port split info.
     *
     * @param[in] hw_id port HW lane id.
     * @param[in] mode split mode for which info requested.
     * @param[out] info pointer to split info structure.
     *
     * @return 0, sai status converted to errno otherwise.
     */
    int (*split_info_get)(uint32_t hw_id, enum ops_sai_port_split mode,
                          struct split_info *info);
    /*
     * Split port.
     *
     * @param[in] hw_id parent port HW lane id.
     * @param[in] mode port split mode.
     * @param[in] speed port speed.
     * @param[in] sub_intf_hw_id_cnt count of sub-interfaces HW IDs.
     * @param[in] sub_intf_hw_id list of sub-interfaces HW IDs.
     *
     * @return 0, sai status converted to errno otherwise.
     */
    int (*split)(uint32_t hw_id, enum ops_sai_port_split mode, uint32_t speed,
                 uint32_t sub_intf_hw_id_cnt, const uint32_t *sub_intf_hw_id);
    /*
     * De-initialize port functionality.
     */
    void (*deinit)(void);
};

DECLARE_GENERIC_CLASS_GETTER(struct port_class, port);

#define ops_sai_port_class_generic() (CLASS_GENERIC_GETTER(port)())

#ifndef ops_sai_port_class
#define ops_sai_port_class           ops_sai_port_class_generic
#endif

typedef void (*port_transaction_clb_t)(uint32_t);

void ops_sai_port_init(void);
int ops_sai_port_transaction_register_callback(port_transaction_clb_t,
                                               enum ops_sai_port_transaction);
int ops_sai_port_transaction_unregister_callback(port_transaction_clb_t);
void ops_sai_port_transaction(uint32_t, enum ops_sai_port_transaction);

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
int ops_sai_port_split_info_get(uint32_t, enum ops_sai_port_split,
                                struct split_info *);
int ops_sai_port_split(uint32_t, enum ops_sai_port_split, uint32_t,
                       uint32_t, const uint32_t *);
void ops_sai_port_deinit(void);

#endif /* sai-port.h */
