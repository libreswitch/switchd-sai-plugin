/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#include <netdev.h>

#include <sai-log.h>
#include <sai-common.h>
#include <sai-port.h>
#include <sai-api-class.h>

/* should be included last due to defines conflicts */
#include <sai-vendor-util.h>

VLOG_DEFINE_THIS_MODULE(mlnx_sai_port);

static void
__port_init(void)
{
    ops_sai_port_class_generic()->init();
}

static void
__port_deinit(void)
{
    ops_sai_port_class_generic()->deinit();
}

static int
__port_config_get(uint32_t hw_id, struct ops_sai_port_config *conf)
{
    return ops_sai_port_class_generic()->config_get(hw_id, conf);
}

static int
__port_config_set(uint32_t hw_id, const struct ops_sai_port_config *new,
                        struct ops_sai_port_config *old)
{
    return ops_sai_port_class_generic()->config_set(hw_id, new, old);
}

static int
__port_mtu_get(uint32_t hw_id, int *mtu)
{
    return ops_sai_port_class_generic()->mtu_get(hw_id, mtu);
}

static int
__port_mtu_set(uint32_t hw_id, int mtu)
{
    return ops_sai_port_class_generic()->mtu_set(hw_id, mtu);
}

static int
__port_carrier_get(uint32_t hw_id, bool *carrier)
{
    return ops_sai_port_class_generic()->carrier_get(hw_id, carrier);
}

static int
__port_flags_update(uint32_t hw_id, enum netdev_flags off,
                    enum netdev_flags on, enum netdev_flags *old_flagsp)
{
    return ops_sai_port_class_generic()->flags_update(hw_id, off, on,
                                                      old_flagsp);
}

static int
__port_pvid_get(uint32_t hw_id, sai_vlan_id_t *pvid)
{
    return ops_sai_port_class_generic()->pvid_get(hw_id, pvid);
}

static int
__port_pvid_set(uint32_t hw_id, sai_vlan_id_t pvid)
{
    return ops_sai_port_class_generic()->pvid_set(hw_id, pvid);
}

static int
__port_stats_get(uint32_t hw_id, struct netdev_stats *stats)
{
    return ops_sai_port_class_generic()->stats_get(hw_id, stats);
}

static int
__port_split_info_get(uint32_t hw_id, enum ops_sai_port_split mode,
                          struct split_info *info)
{
    int status = 0;
    enum mlnx_platform_type type = MLNX_PLATFORM_TYPE_UNKNOWN;

    info->disable_neighbor = false;

    if (mode == OPS_SAI_PORT_SPLIT_TO_4) {
        status = ops_sai_mlnx_platform_type_get(&type);
        ERRNO_LOG_EXIT(status, "Failed to get platform type");

        switch(type) {
        case MLNX_PLATFORM_TYPE_SN2700:
        case MLNX_PLATFORM_TYPE_SN2410:
            info->disable_neighbor = true;
            info->neighbor_hw_id = hw_id + SAI_MAX_LANES;
            break;
        default:
            break;
        }
    }

exit:
    return status;
}

static int
__port_split(uint32_t hw_id, enum ops_sai_port_split mode, uint32_t speed,
                 uint32_t sub_intf_hw_id_cnt,
                 const uint32_t *sub_intf_hw_id)
{
    return ops_sai_port_class_generic()->split(hw_id, mode, speed,
                                               sub_intf_hw_id_cnt,
                                               sub_intf_hw_id);
}

DEFINE_VENDOR_CLASS(struct port_class, port) = {
        .init = __port_init,
        .config_get = __port_config_get,
        .config_set = __port_config_set,
        .mtu_get = __port_mtu_get,
        .mtu_set = __port_mtu_set,
        .carrier_get = __port_carrier_get,
        .flags_update = __port_flags_update,
        .pvid_get = __port_pvid_get,
        .pvid_set = __port_pvid_set,
        .stats_get = __port_stats_get,
        .split_info_get = __port_split_info_get,
        .split = __port_split,
        .deinit = __port_deinit,
};

DEFINE_VENDOR_CLASS_GETTER(struct port_class, port);
