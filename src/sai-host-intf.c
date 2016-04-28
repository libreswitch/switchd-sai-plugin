/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#include <util.h>

#include <sai-log.h>
#include <sai-api-class.h>

VLOG_DEFINE_THIS_MODULE(sai_hostint);

static const sai_hostif_trap_id_t hostif_traps[] = {
    SAI_HOSTIF_TRAP_ID_LLDP,
};

/**
 * Creates host interface.
 * @param[in] name interface name.
 * @param[in] hw_id port label id.
 * @return 0, sai error converted to errno otherwise.
 */
int
ops_sai_hostint_netdev_create(const char *name, uint32_t hw_id)
{
    sai_attribute_t hostif_attrib[3] = { };
    sai_status_t status = SAI_STATUS_SUCCESS;
    sai_object_id_t hif_id_port = SAI_NULL_OBJECT_ID;
    const struct ops_sai_api_class *sai_api = ops_sai_api_get_instance();

    NULL_PARAM_LOG_ABORT(name);

    hostif_attrib[0].id = SAI_HOSTIF_ATTR_TYPE;
    hostif_attrib[0].value.s32 = SAI_HOSTIF_TYPE_NETDEV;
    hostif_attrib[1].id = SAI_HOSTIF_ATTR_NAME;
    strcpy(hostif_attrib[1].value.chardata, name);
    hostif_attrib[2].id = SAI_HOSTIF_ATTR_RIF_OR_PORT_ID;
    hostif_attrib[2].value.oid = ops_sai_api_hw_id2port_id(hw_id);

    status = sai_api->host_interface_api->create_hostif(&hif_id_port,
                                                        ARRAY_SIZE(hostif_attrib),
                                                        hostif_attrib);

    return SAI_ERROR_2_ERRNO(status);
}

/**
 * Registers traps for packets.
 */
void
ops_sai_hostint_traps_register(void)
{
    sai_attribute_t attr = { };
    sai_status_t status = SAI_STATUS_SUCCESS;
    const struct ops_sai_api_class *sai_api = ops_sai_api_get_instance();

    for (int i = 0; i < ARRAY_SIZE(hostif_traps); ++i) {
        attr.id = SAI_HOSTIF_TRAP_ATTR_PACKET_ACTION;
        attr.value.s32 = SAI_PACKET_ACTION_TRAP;
        status = sai_api->host_interface_api->set_trap_attribute(hostif_traps[i],
                                                                &attr);
        SAI_ERROR_LOG_ABORT(status, "Failed to set trap packet action %d",
                            hostif_traps[i]);

        attr.id = SAI_HOSTIF_TRAP_ATTR_TRAP_CHANNEL;
        attr.value.s32 = SAI_HOSTIF_TRAP_CHANNEL_NETDEV;
        status = sai_api->host_interface_api->set_trap_attribute(hostif_traps[i],
                                                                 &attr);
        SAI_ERROR_LOG_ABORT(status, "Failed to set trap channel %d",
                            hostif_traps[i]);
    }
}
