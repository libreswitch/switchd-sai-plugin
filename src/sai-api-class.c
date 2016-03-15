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
#include <openvswitch/vlog.h>

VLOG_DEFINE_THIS_MODULE(sai_api_class);

static struct sai_api_class sai_api;
static sai_object_id_t sai_lable_id_to_oid_map[SAI_PORTS_MAX];

static const char *sai_profile_get_value(sai_switch_profile_id_t,
                                         const char *);
static int sai_profile_get_next_value(sai_switch_profile_id_t, const char **,
                                      const char **);
static void sai_event_switch_state_changed(sai_switch_oper_status_t);
static void sai_event_fdb(uint32_t, sai_fdb_event_notification_data_t *);
static void sai_event_port_state(uint32_t,
                                 sai_port_oper_status_notification_t *);
static void sai_event_port(uint32_t, sai_port_event_notification_t *);
static void sai_event_switch_shutdown(void);
static void sai_event_rx_packet(const void *, sai_size_t, uint32_t,
                                const sai_attribute_t *);
static sai_status_t sai_api_get_port_lable_id(sai_object_id_t, uint32_t *);
static sai_status_t sai_api_init_ports(void);

int
sai_api_init(void)
{
    sai_status_t status = SAI_STATUS_SUCCESS;

    static const service_method_table_t sai_services = {
        sai_profile_get_value,
        sai_profile_get_next_value,
    };
    static sai_switch_notification_t sai_events = {
        sai_event_switch_state_changed,
        sai_event_fdb,
        sai_event_port_state,
        sai_event_port,
        sai_event_switch_shutdown,
        sai_event_rx_packet,
    };

    SAI_API_TRACE_FN();

    if (sai_api.initialized) {
        status = SAI_STATUS_FAILURE;
        SAI_ERROR_LOG_EXIT(status, "SAI api already initialized");
    }

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

    status = sai_api_init_ports();
    SAI_ERROR_LOG_EXIT(status, "Failed to create interfaces");

    sai_api.initialized = true;

exit:
    if (SAI_ERROR(status)) {
        ovs_assert(false);
    }

    return 0;
}

int
sai_api_uninit(void)
{
    sai_status_t status = SAI_STATUS_SUCCESS;

    SAI_API_TRACE_FN();

    status = sai_api_uninitialize();
    SAI_ERROR_LOG_EXIT(status, "Failed to uninitialize SAI api");

exit:
    return SAI_ERROR(status);
}

const struct sai_api_class *
sai_api_get_instance(void)
{
    ovs_assert(sai_api.initialized);
    return &sai_api;
}

sai_object_id_t
sai_api_hw_id2port_id(uint32_t hw_id)
{
    return sai_lable_id_to_oid_map[hw_id];
}

static const char *
sai_profile_get_value(sai_switch_profile_id_t profile_id, const char *variable)
{
    SAI_API_TRACE_FN();

    /* Temporarily hardcoded values until issue with reading data from EEPROM
     * will be fixed. */
    if (!strcmp(variable, SAI_KEY_INIT_CONFIG_FILE)) {
        return "/usr/share/sai_2700.xml";
    } else if (!strcmp(variable, "DEVICE_MAC_ADDRESS")) {
        return "20:03:04:05:06:00";
    } else if (!strcmp(variable, "INITIAL_FAN_SPEED")) {
        return "50";
    }

    return NULL;
}

static int
sai_profile_get_next_value(sai_switch_profile_id_t profile_id,
                           const char **variable, const char **value)
{
    SAI_API_TRACE_FN();

    return -1;
}

static void
sai_event_switch_state_changed(sai_switch_oper_status_t switch_oper_status)
{
    SAI_API_TRACE_FN();
}

static void
sai_event_fdb(uint32_t count, sai_fdb_event_notification_data_t * data)
{
    SAI_API_TRACE_FN();
}

static void
sai_event_port_state(uint32_t count,
                     sai_port_oper_status_notification_t * data)
{
    uint32_t i = 0;

    SAI_API_TRACE_FN();

    for (i = 0; i < count; i++) {
        netdev_sai_port_oper_state_changed(data[i].port_id,
                                           SAI_PORT_OPER_STATUS_UP ==
                                           data[i].port_state);
    }
}

static void
sai_event_port(uint32_t count, sai_port_event_notification_t * data)
{
    SAI_API_TRACE_FN();
}

static void
sai_event_switch_shutdown(void)
{
    SAI_API_TRACE_FN();
}

static void
sai_event_rx_packet(const void *buffer, sai_size_t buffer_size,
                    uint32_t attr_count, const sai_attribute_t * attr_list)
{
    SAI_API_TRACE_FN();
}

static sai_status_t
sai_api_get_port_lable_id(sai_object_id_t oid, uint32_t *label_id)
{
    sai_attribute_t attr;
    uint32_t hw_lanes[SAI_MAX_LANES];
    sai_status_t status = SAI_STATUS_SUCCESS;

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

static sai_status_t
sai_api_init_ports(void)
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
        status = sai_api_get_port_lable_id(sai_oids[i], &label_id);
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
