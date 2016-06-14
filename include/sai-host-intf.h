/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#ifndef SAI_HOST_INTF_H
#define SAI_HOST_INTF_H 1

#include <sai-common.h>
#include <sai-policer.h>
#ifdef SAI_VENDOR
#include <sai-vendor-common.h>
#endif /* SAI_VENDOR */

enum host_intf_type {
    HOST_INTF_TYPE_L2_PORT_NETDEV,
    HOST_INTF_TYPE_L3_PORT_NETDEV,
    HOST_INTF_TYPE_L3_VLAN_NETDEV,
    __HOST_INTF_TYPE_MAX,
    HOST_INTF_TYPE_MIN = HOST_INTF_TYPE_L2_PORT_NETDEV,
    HOST_INTF_TYPE_MAX = __HOST_INTF_TYPE_MAX - 1,
};

struct host_intf_class {
    /**
     * Initialize host interface.
     */
    void (*init)(void);
    /**
     * Creates Linux netdev for specified interface.
     *
     * @param[in] name   - netdev name.
     * @param[in] type   - netdev type.
     * @param[in] handle - if netdev is L2 specifies port label id else VLAN id.
     * @param[in] mac    - netdev mac address.
     *
     * @return 0 operation completed successfully
     * @return errno operation failed
     */
    int (*create)(const char            *name,
                  enum host_intf_type   type,
                  const handle_t        *handle,
                  const struct eth_addr *mac);
    /**
     * Removes Linux netdev for specified interface.
     *
     * @param[in] name - netdev name
     *
     * @return 0 operation completed successfully
     * @return errno operation failed
     */
    int (*remove)(const char *name);
    /**
    * Registers traps for packets.
    */
    void (*traps_register)(void);
    /**
    * Unregisters traps for packets.
    */
    void (*traps_unregister)(void);
    /**
    * De-initialize host interface.
    */
    void (*deinit)(void);
};

DECLARE_GENERIC_CLASS_GETTER(struct host_intf_class, host_intf);

#define ops_sai_host_intf_class_generic() (CLASS_GENERIC_GETTER(host_intf)())

#ifndef ops_sai_host_intf_class
#define ops_sai_host_intf_class           ops_sai_host_intf_class_generic
#endif

#define SAI_TRAP_GROUP_MAX_NAME_LEN 50
#define SAI_TRAP_ID_MAX_COUNT 10

struct ops_sai_trap_group_config {
    const char *name;
    int trap_ids[SAI_TRAP_ID_MAX_COUNT];
    struct ops_sai_policer_config policer_config;
    uint32_t priority;
    bool is_log;
    bool is_l3;
};

struct ops_sai_trap_group_entry {
    char name[SAI_TRAP_GROUP_MAX_NAME_LEN];
    struct ovs_list list_node;
    handle_t trap_group;
    handle_t policer;
};

static inline void ops_sai_host_intf_init(void)
{
    ovs_assert(ops_sai_host_intf_class()->init);
    ops_sai_host_intf_class()->init();

}

static inline int ops_sai_host_intf_netdev_create(const char * name,
                                                  enum host_intf_type type,
                                                  const handle_t *handle,
                                                  const struct eth_addr *mac)
{
    ovs_assert(ops_sai_host_intf_class()->create);
    return ops_sai_host_intf_class()->create(name, type, handle, mac);
}

static inline int ops_sai_host_intf_netdev_remove(const char *name)
{
    ovs_assert(ops_sai_host_intf_class()->remove);
    return ops_sai_host_intf_class()->remove(name);
}

static inline void ops_sai_host_intf_traps_register(void)
{
    ovs_assert(ops_sai_host_intf_class()->traps_register);
    ops_sai_host_intf_class()->traps_register();
}

static inline void ops_sai_host_intf_traps_unregister(void)
{
    ovs_assert(ops_sai_host_intf_class()->traps_unregister);
    ops_sai_host_intf_class()->traps_unregister();
}

static inline void ops_sai_host_intf_deinit(void)
{
    ovs_assert(ops_sai_host_intf_class()->deinit);
    ops_sai_host_intf_class()->deinit();
}

const char *ops_sai_host_intf_type_to_str(enum host_intf_type);

#endif /* sai-host-intf.h */
