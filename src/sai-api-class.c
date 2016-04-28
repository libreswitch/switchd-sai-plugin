/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.  
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#include <malloc.h>
#include <string.h>
#include <sai-api-class.h>
#include <sai-log.h>
#include <sai-netdev.h>
#include <util.h>
#include <sai-vendor.h>

#ifndef SAI_INIT_CONFIG_FILE_PATH
#define SAI_INIT_CONFIG_FILE_PATH ""
#endif

#define MAC_STR_LEN 17

VLOG_DEFINE_THIS_MODULE(sai_api_class);

static struct ops_sai_api_class sai_api;
static sai_object_id_t sai_lable_id_to_oid_map[SAI_PORTS_MAX];
static char sai_api_mac_str[MAC_STR_LEN + 1];

static const char *__profile_get_value(sai_switch_profile_id_t, const char *);
static int __profile_get_next_value(sai_switch_profile_id_t, const char **,
                                    const char **);
static void __event_switch_state_changed(sai_switch_oper_status_t);
static void __event_fdb(uint32_t, sai_fdb_event_notification_data_t *);
static void __event_port_state(uint32_t,
                               sai_port_oper_status_notification_t *);
static void __event_port(uint32_t, sai_port_event_notification_t *);
static void __event_switch_shutdown(void);
static void __event_rx_packet(const void *, sai_size_t, uint32_t,
                                const sai_attribute_t *);
static sai_status_t __get_port_lable_id(sai_object_id_t, uint32_t *);
static sai_status_t __init_ports(void);

/**
 * Initialize SAI api. Register callbacks, query APIs.
 */
void
ops_sai_api_init(void)
{
    sai_status_t status = SAI_STATUS_SUCCESS;
    sai_mac_t mac = { };

    static const service_method_table_t sai_services = {
        __profile_get_value,
        __profile_get_next_value,
    };
    static sai_switch_notification_t sai_events = {
        __event_switch_state_changed,
        __event_fdb,
        __event_port_state,
        __event_port,
        __event_switch_shutdown,
        __event_rx_packet,
    };

    SAI_API_TRACE_FN();

    if (sai_api.initialized) {
        status = SAI_STATUS_FAILURE;
        SAI_ERROR_LOG_EXIT(status, "SAI api already initialized");
    }

    status = ops_sai_vendor_base_mac_get(mac);
    SAI_ERROR_LOG_EXIT(status, "Failed to get base MAC address");
    sprintf(sai_api_mac_str, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    status = sai_api_initialize(0, &sai_services);
    SAI_ERROR_LOG_EXIT(status, "Failed to initialize SAI api");

    status = sai_api_query(SAI_API_SWITCH, (void **) &sai_api.switch_api);
    SAI_ERROR_LOG_EXIT(status, "Failed to initialize SAI switch api");

    status = sai_api_query(SAI_API_PORT, (void **) &sai_api.port_api);
    SAI_ERROR_LOG_EXIT(status, "Failed to initialize SAI port api");

    status = sai_api_query(SAI_API_VLAN, (void **) &sai_api.vlan_api);
    SAI_ERROR_LOG_EXIT(status, "Failed to initialize SAI vlan api");

    status = sai_api_query(SAI_API_HOST_INTERFACE,
                           (void **) &sai_api.host_interface_api);
    SAI_ERROR_LOG_EXIT(status, "Failed to initialize SAI host interface api");

    status = sai_api.switch_api->initialize_switch(1, "SX", "/", &sai_events);
    SAI_ERROR_LOG_EXIT(status, "Failed to initialize switch");

    status = __init_ports();
    SAI_ERROR_LOG_EXIT(status, "Failed to create interfaces");

    sai_api.initialized = true;

exit:
    if (SAI_ERROR_2_ERRNO(status)) {
        ovs_assert(false);
    }
}

/**
 * Uninitialie SAI api.
 */
int
ops_sai_api_uninit(void)
{
    sai_status_t status = SAI_STATUS_SUCCESS;

    SAI_API_TRACE_FN();

    sai_api.initialized = false;
    status = sai_api_uninitialize();
    SAI_ERROR_LOG_EXIT(status, "Failed to uninitialize SAI api");

exit:
    return SAI_ERROR_2_ERRNO(status);
}

/**
 * Get SAI api class. API has to be alreadyu initialize with sai_api_init().
 * @return pointer to sai_api class.
 */
const struct ops_sai_api_class *
ops_sai_api_get_instance(void)
{
    ovs_assert(sai_api.initialized);
    return &sai_api;
}

/**
 * Convert port label ID to sai_object_id_t.
 * @return sai_object_id_t of requested port.
 */
sai_object_id_t
ops_sai_api_hw_id2port_id(uint32_t hw_id)
{
    return sai_lable_id_to_oid_map[hw_id];
}

/*
 * Return value requested by SAI using string key.
 */
static const char *
__profile_get_value(sai_switch_profile_id_t profile_id, const char *variable)
{
    SAI_API_TRACE_FN();

    NULL_PARAM_LOG_ABORT(variable);

    if (!strcmp(variable, SAI_KEY_INIT_CONFIG_FILE)) {
        return SAI_INIT_CONFIG_FILE_PATH;
    } else if (!strcmp(variable, "DEVICE_MAC_ADDRESS")) {
        return sai_api_mac_str;
    } else if (!strcmp(variable, "INITIAL_FAN_SPEED")) {
        return "50";
    }

    return NULL;
}

/*
 * Return next value requested by SAI using string key.
 */
static int
__profile_get_next_value(sai_switch_profile_id_t profile_id,
                           const char **variable, const char **value)
{
    SAI_API_TRACE_FN();

    return -1;
}

/*
 * Function will be called by SAI when switch state changes.
 */
static void
__event_switch_state_changed(sai_switch_oper_status_t switch_oper_status)
{
    SAI_API_TRACE_FN();
}

/*
 * Function will be called by SAI on fdb event.
 */
static void
__event_fdb(uint32_t count, sai_fdb_event_notification_data_t * data)
{
    SAI_API_TRACE_FN();
}

/*
 * Function will be called by SAI when port state changes.
 */
static void
__event_port_state(uint32_t count,
                   sai_port_oper_status_notification_t * data)
{
    uint32_t i = 0;

    SAI_API_TRACE_FN();

    NULL_PARAM_LOG_ABORT(data);

    for (i = 0; i < count; i++) {
        netdev_sai_port_oper_state_changed(data[i].port_id,
                                           SAI_PORT_OPER_STATUS_UP ==
                                           data[i].port_state);
    }
}

/*
 * Function will be called by SAI on port event.
 */
static void
__event_port(uint32_t count, sai_port_event_notification_t * data)
{
    SAI_API_TRACE_FN();
}

/*
 * Function will be called by SAI on switch shutdown.
 */
static void
__event_switch_shutdown(void)
{
    SAI_API_TRACE_FN();
}

/*
 * Function will be called by SAI on rx packet.
 */
static void
__event_rx_packet(const void *buffer, sai_size_t buffer_size,
                  uint32_t attr_count, const sai_attribute_t * attr_list)
{
    SAI_API_TRACE_FN();
}

/*
 * Get port label ID bi sai_object_id_t.
 */
static sai_status_t
__get_port_lable_id(sai_object_id_t oid, uint32_t *label_id)
{
    sai_attribute_t attr;
    uint32_t hw_lanes[SAI_MAX_LANES];
    sai_status_t status = SAI_STATUS_SUCCESS;

    NULL_PARAM_LOG_ABORT(label_id);

    attr.id = SAI_PORT_ATTR_HW_LANE_LIST;
    attr.value.u32list.count = SAI_MAX_LANES;
    attr.value.u32list.list = hw_lanes;

    status = sai_api.port_api->get_port_attribute(oid, 1, &attr);
    SAI_ERROR_LOG_EXIT(status, "Failed to get port HW lane list (port: %lu)",
                       oid);

    if (attr.value.u32list.count < 1) {
        status = SAI_STATUS_FAILURE;
        goto exit;
    }

    *label_id = (hw_lanes[0] / attr.value.u32list.count) + 1;
    VLOG_DBG("Port label id: %u", *label_id);

exit:
    return status;
}

/*
 * Initialize physical ports list.
 */
static sai_status_t
__init_ports(void)
{
    uint32_t label_id = 0;
    sai_uint32_t port_number = 0;
    sai_attribute_t switch_attrib = { };
    sai_status_t status = SAI_STATUS_SUCCESS;
    static sai_object_id_t sai_oids[SAI_PORTS_MAX];

    switch_attrib.id = SAI_SWITCH_ATTR_PORT_NUMBER;
    status = sai_api.switch_api->get_switch_attribute(1, &switch_attrib);
    SAI_ERROR_LOG_EXIT(status, "Failed to get switch port number");

    port_number = switch_attrib.value.u32;
    switch_attrib.id = SAI_SWITCH_ATTR_PORT_LIST;
    switch_attrib.value.objlist.count = port_number;
    switch_attrib.value.objlist.list = sai_oids;
    status = sai_api.switch_api->get_switch_attribute(1, &switch_attrib);
    SAI_ERROR_LOG_EXIT(status, "Failed to get switch port list");

    for (int i = 0; i < port_number; ++i) {
        status = __get_port_lable_id(sai_oids[i], &label_id);
        SAI_ERROR_LOG_EXIT(status, "Failed to get switch port list");

        if (label_id > port_number) {
            status = SAI_STATUS_BUFFER_OVERFLOW;
            SAI_ERROR_LOG_EXIT(status, "label_id is too large");
        }

        sai_lable_id_to_oid_map[label_id] = sai_oids[i];
    }

exit:
    return status;
}
