/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.  
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#include <errno.h>

#include <seq.h>
#include <coverage.h>
#include <vlan-bitmap.h>
#include <ofproto/ofproto-provider.h>
#include <ofproto/bond.h>
#include <ofproto/tunnel.h>
#include <openvswitch/vlog.h>

#include <vswitch-idl.h>
#include <openswitch-idl.h>

#include <sai-netdev.h>
#include <sai-api-class.h>
#include <sai-log.h>

#define SAI_INTERFACE_TYPE_SYSTEM "system"
#define SAI_INTERFACE_TYPE_VRF "vrf"
#define SAI_DATAPATH_VERSION "0.0.1"

VLOG_DEFINE_THIS_MODULE(ofproto_sai);

struct ofproto_sai {
    struct ofproto up;
    struct hmap_node all_ofproto_sai_node;      /* In 'all_ofproto_dpifs'. */
    struct hmap bundles;        /* Contains "struct ofbundle"s. */
    struct ovs_mutex mutex;
    struct sset ports;          /* Set of standard port names. */
    struct sset ghost_ports;    /* Ports with no datapath port. */
};

struct ofport_sai {
    struct ofport up;
    struct ofbundle_sai *bundle;        /* Bundle that contains this port */
    struct ovs_list bundle_node;        /* In struct ofbundle's "ports" list. */
    sai_object_id_t port_oid;   /* SAI port ID */
};

struct ofbundle_sai {
    struct hmap_node hmap_node; /* In struct ofproto's "bundles" hmap. */
    struct ofproto_sai *ofproto;        /* Owning ofproto. */
    void *aux;                  /* Key supplied by ofproto's client. */
    char *name;                 /* Identifier for log messages. */

    /* Configuration. */
    struct ovs_list ports;      /* Contains "struct ofport"s. */
    enum port_vlan_mode vlan_mode;      /* VLAN mode */
    int vlan;                   /* -1=trunk port, else a 12-bit VLAN ID. */
    unsigned long *trunks;      /* Bitmap of trunked VLANs, if 'vlan' == -1.
                                 * NULL if all VLANs are trunked. */
};

struct ofproto_sai_group {
    struct ofgroup up;
};

struct port_dump_state {
    uint32_t bucket;
    uint32_t offset;
    bool ghost;
    struct ofproto_port port;
    bool has_port;
};

/* All existing ofproto provider instances, indexed by ->up.name. */
static struct hmap all_ofproto_sai = HMAP_INITIALIZER(&all_ofproto_sai);

static const sai_hostif_trap_id_t hostif_traps[] = {
    SAI_HOSTIF_TRAP_ID_LLDP,
};

static void ofproto_sai_init(const struct shash *);
static void ofproto_sai_enumerate_types(struct sset *);
static int ofproto_sai_enumerate_names(const char *, struct sset *);
static int ofproto_sai_del(const char *, const char *);
static const char *ofproto_sai_port_open_type(const char *, const char *);
static struct ofproto *ofproto_sai_alloc(void);
static inline struct ofproto_sai *ofproto_sai_cast(const struct ofproto *);
static int ofproto_sai_construct(struct ofproto *);
static void ofproto_sai_destruct(struct ofproto *);
static void ofproto_sai_dealloc(struct ofproto *);
static inline struct ofport_sai *ofport_sai_cast(const struct ofport *);
static struct ofport *ofproto_sai_port_alloc(void);
static int ofproto_sai_port_construct(struct ofport *);
static void ofproto_sai_port_destruct(struct ofport *);
static void ofproto_sai_port_dealloc(struct ofport *);
static void ofproto_sai_port_reconfigured(struct ofport *,
                                          enum ofputil_port_config);
static int ofproto_sai_port_query_by_name(const struct ofproto *, const char *,
                                          struct ofproto_port *);
static struct ofport_sai *ofproto_sai_get_ofp_port(const struct ofproto_sai *,
                                                   ofp_port_t);
static int ofproto_sai_port_add(struct ofproto *, struct netdev *netdev);
static int ofproto_sai_port_del(struct ofproto *, ofp_port_t);
static int ofproto_sai_port_get_stats(const struct ofport *,
                                      struct netdev_stats *);
static int ofproto_sai_port_dump_start(const struct ofproto *, void **);
static int ofproto_sai_port_dump_next(const struct ofproto *, void *,
                                      struct ofproto_port *);
static int ofproto_sai_port_dump_done(const struct ofproto *, void *);
static struct rule *ofproto_sai_rule_alloc(void);
static void ofproto_sai_rule_dealloc(struct rule *);
static enum ofperr ofproto_sai_rule_construct(struct rule *);
static void ofproto_sai_rule_insert(struct rule *, struct rule *, bool);
static void ofproto_sai_rule_delete(struct rule *);
static void ofproto_sai_rule_destruct(struct rule *);
static void ofproto_sai_rule_get_stats(struct rule *, uint64_t *, uint64_t *,
                                       long long int *);
static enum ofperr ofproto_sai_rule_execute(struct rule *, const struct flow *,
                                            struct dp_packet *);
static bool ofproto_sai_set_frag_handling(struct ofproto *,
                                          enum ofp_config_flags);
static enum ofperr ofproto_sai_packet_out(struct ofproto *, struct dp_packet *,
                                          const struct flow *,
                                          const struct ofpact *, size_t);
static sai_status_t ofbundle_sai_port_vlan_set(const struct ofport_sai *,
                                               sai_vlan_id_t,
                                               sai_vlan_tagging_mode_t, bool);
static sai_status_t ofbundle_sai_port_vlan_add(const struct ofport_sai *,
                                               sai_vlan_id_t,
                                               sai_vlan_tagging_mode_t);
static sai_status_t ofbundle_sai_port_vlan_del(const struct ofport_sai *,
                                               sai_vlan_id_t);
static sai_status_t ofbundle_sai_port_vlan_add(const struct ofport_sai *port,
                                               sai_vlan_id_t vid,
                                               sai_vlan_tagging_mode_t mode);
static sai_status_t ofbundle_sai_port_trunks_set(const struct ofport_sai *,
                                                 unsigned long *,
                                                 sai_vlan_tagging_mode_t,
                                                 bool);
static sai_status_t ofbundle_sai_port_trunks_add(struct ofport_sai *,
                                                 unsigned long *,
                                                 sai_vlan_tagging_mode_t);
static sai_status_t ofbundle_sai_port_trunks_del(struct ofport_sai *,
                                                 unsigned long *);
static sai_vlan_tagging_mode_t ofbundle_sai_vlan_mode(enum port_vlan_mode,
                                                      bool);
static sai_status_t ofbundle_sai_port_pvid_vlan_del(struct ofport_sai *);
static sai_status_t ofbundle_sai_port_add(struct ofbundle_sai *,
                                          struct ofport_sai *);
static sai_status_t ofbundle_sai_port_del(struct ofport_sai *);
static void ofbundle_sai_trunks_realloc(struct ofbundle_sai *,
                                        const unsigned long *);
static sai_status_t ofbundle_sai_vlan_reconfigure(struct ofbundle_sai *,
                                                  const struct
                                                  ofproto_bundle_settings *);
static sai_status_t ofbundle_sai_ports_reconfigure(struct ofbundle_sai *,
                                                   const struct
                                                   ofproto_bundle_settings *);
static void ofbundle_sai_rename(struct ofbundle_sai *, const char *);
static struct ofbundle_sai *ofbundle_sai_create(struct ofproto_sai *, void *,
                                                const struct
                                                ofproto_bundle_settings *);
static void ofbundle_sai_destroy(struct ofbundle_sai *);
static struct ofbundle_sai *ofbundle_sai_lookup(struct ofproto_sai *, void *);
static int ofproto_sai_bundle_set(struct ofproto *, void *,
                                  const struct ofproto_bundle_settings *);
static void ofproto_sai_bundle_remove(struct ofport *);
static int ofproto_sai_bundle_get(struct ofproto *, void *, int *);
static int ofproto_sai_set_vlan(struct ofproto *, int, bool);
static inline struct ofproto_sai_group *ofproto_sai_group_cast(const struct
                                                               ofgroup *);
static struct ofgroup *ofproto_sai_group_alloc(void);
static enum ofperr ofproto_sai_group_construct(struct ofgroup *);
static void ofproto_sai_group_destruct(struct ofgroup *);
static void ofproto_sai_group_dealloc(struct ofgroup *);
static enum ofperr ofproto_sai_group_modify(struct ofgroup *);
static enum ofperr ofproto_sai_group_get_stats(const struct ofgroup *,
                                               struct ofputil_group_stats *);
static const char *ofproto_sai_get_datapath_version(const struct ofproto *);
static int ofproto_sai_add_l3_host_entry(const struct ofproto *, void *, bool,
                                         char *, char *, int *);
static int ofproto_sai_delete_l3_host_entry(const struct ofproto *, void *,
                                            bool, char *, int *);
static int ofproto_sai_get_l3_host_hit_bit(const struct ofproto *, void *,
                                           bool, char *, bool *);
static int ofproto_sai_l3_route_action(const struct ofproto *,
                                       enum ofproto_route_action,
                                       struct ofproto_route *);
static int ofproto_sai_l3_ecmp_set(const struct ofproto *, bool);
static int ofproto_sai_l3_ecmp_hash_set(const struct ofproto *, unsigned int,
                                        bool);
static int ofproto_sai_run(struct ofproto *);
static void ofproto_sai_wait(struct ofproto *);
static void ofproto_sai_set_tables_version(struct ofproto *, cls_version_t);

const struct ofproto_class ofproto_sai_class = {
    .init                    = ofproto_sai_init,
    .enumerate_types         = ofproto_sai_enumerate_types,
    .enumerate_names         = ofproto_sai_enumerate_names,
    .del                     = ofproto_sai_del,
    .port_open_type          = ofproto_sai_port_open_type,
    .type_run                = NULL,
    .type_wait               = NULL,
    .alloc                   = ofproto_sai_alloc,
    .construct               = ofproto_sai_construct,
    .destruct                = ofproto_sai_destruct,
    .dealloc                 = ofproto_sai_dealloc,
    .run                     = ofproto_sai_run,
    .wait                    = ofproto_sai_wait,
    .get_memory_usage        = NULL,
    .type_get_memory_usage   = NULL,
    .flush                   = NULL,
    .query_tables            = NULL,
    .set_tables_version      = ofproto_sai_set_tables_version,
    .port_alloc              = ofproto_sai_port_alloc,
    .port_construct          = ofproto_sai_port_construct,
    .port_destruct           = ofproto_sai_port_destruct,
    .port_dealloc            = ofproto_sai_port_dealloc,
    .port_modified           = NULL,
    .port_reconfigured       = ofproto_sai_port_reconfigured,
    .port_query_by_name      = ofproto_sai_port_query_by_name,
    .port_add                = ofproto_sai_port_add,
    .port_del                = ofproto_sai_port_del,
    .port_get_stats          = ofproto_sai_port_get_stats,
    .port_dump_start         = ofproto_sai_port_dump_start,
    .port_dump_next          = ofproto_sai_port_dump_next,
    .port_dump_done          = ofproto_sai_port_dump_done,
    .port_poll               = NULL,
    .port_poll_wait          = NULL,
    .port_is_lacp_current    = NULL,
    .port_get_lacp_stats     = NULL,
    .rule_construct          = NULL,
    .rule_alloc              = ofproto_sai_rule_alloc,
    .rule_construct          = ofproto_sai_rule_construct,
    .rule_insert             = ofproto_sai_rule_insert,
    .rule_delete             = ofproto_sai_rule_delete,
    .rule_destruct           = ofproto_sai_rule_destruct,
    .rule_dealloc            = ofproto_sai_rule_dealloc,
    .rule_get_stats          = ofproto_sai_rule_get_stats,
    .rule_execute            = ofproto_sai_rule_execute,
    .set_frag_handling       = ofproto_sai_set_frag_handling,
    .packet_out              = ofproto_sai_packet_out,
    .set_netflow             = NULL,
    .get_netflow_ids         = NULL,
    .set_sflow               = NULL,
    .set_ipfix               = NULL,
    .set_cfm                 = NULL,
    .cfm_status_changed      = NULL,
    .get_cfm_status          = NULL,
    .set_bfd                 = NULL,
    .bfd_status_changed      = NULL,
    .get_bfd_status          = NULL,
    .set_stp                 = NULL,
    .get_stp_status          = NULL,
    .set_stp_port            = NULL,
    .get_stp_port_status     = NULL,
    .get_stp_port_stats      = NULL,
    .set_rstp                = NULL,
    .get_rstp_status         = NULL,
    .set_rstp_port           = NULL,
    .get_rstp_port_status    = NULL,
    .set_queues              = NULL,
    .bundle_set              = ofproto_sai_bundle_set,
    .bundle_remove           = ofproto_sai_bundle_remove,
    .bundle_get              = ofproto_sai_bundle_get,
    .set_vlan                = ofproto_sai_set_vlan,
    .mirror_set              = NULL,
    .mirror_get_stats        = NULL,
    .set_flood_vlans         = NULL,
    .is_mirror_output_bundle = NULL,
    .forward_bpdu_changed    = NULL,
    .set_mac_table_config    = NULL,
    .set_mcast_snooping      = NULL,
    .set_mcast_snooping_port = NULL,
    .set_realdev             = NULL,
    .meter_get_features      = NULL,
    .meter_set               = NULL,
    .meter_get               = NULL,
    .meter_del               = NULL,
    .group_alloc             = ofproto_sai_group_alloc,
    .group_construct         = ofproto_sai_group_construct,
    .group_destruct          = ofproto_sai_group_destruct,
    .group_dealloc           = ofproto_sai_group_dealloc,
    .group_modify            = ofproto_sai_group_modify,
    .group_get_stats         = ofproto_sai_group_get_stats,
    .get_datapath_version    = ofproto_sai_get_datapath_version,
    .add_l3_host_entry       = ofproto_sai_add_l3_host_entry,
    .delete_l3_host_entry    = ofproto_sai_delete_l3_host_entry,
    .get_l3_host_hit         = ofproto_sai_get_l3_host_hit_bit,
    .l3_route_action         = ofproto_sai_l3_route_action,
    .l3_ecmp_set             = ofproto_sai_l3_ecmp_set,
    .l3_ecmp_hash_set        = ofproto_sai_l3_ecmp_hash_set,
};

void
ofproto_sai_register(void)
{
    ofproto_class_register(&ofproto_sai_class);
}

static void
ofproto_sai_init(const struct shash *iface_hints)
{
    sai_attribute_t attr = { };
    sai_status_t status = SAI_STATUS_SUCCESS;
    const struct sai_api_class *sai_api = sai_api_get_instance();

    SAI_API_TRACE_FN();

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

static void
ofproto_sai_enumerate_types(struct sset *types)
{
    SAI_API_TRACE_FN();

    /* VRF isn't supported yet, but needed for ops-switchd. */
    sset_add(types, SAI_INTERFACE_TYPE_VRF);
    sset_add(types, SAI_INTERFACE_TYPE_SYSTEM);
}

static int
ofproto_sai_enumerate_names(const char *type, struct sset *names)
{
    struct ofproto_sai *ofproto;

    SAI_API_TRACE_FN();

    sset_clear(names);
    HMAP_FOR_EACH(ofproto, all_ofproto_sai_node, &all_ofproto_sai) {
        if (strcmp(type, ofproto->up.type)) {
            continue;
        }
        sset_add(names, ofproto->up.name);
    }

    return 0;
}

static int
ofproto_sai_del(const char *type, const char *name)
{
    SAI_API_TRACE_FN();

    return 0;
}

static const char *
ofproto_sai_port_open_type(const char *datapath_type, const char *port_type)
{
    SAI_API_TRACE_FN();

    VLOG_DBG("datapath_type: %s, port_type: %s", datapath_type, port_type);

    if ((strcmp(port_type, OVSREC_INTERFACE_TYPE_INTERNAL) == 0) ||
        (strcmp(port_type, OVSREC_INTERFACE_TYPE_LOOPBACK) == 0)) {
        return port_type;
    } else {
        return SAI_INTERFACE_TYPE_SYSTEM;
    }
}

static struct ofproto *
ofproto_sai_alloc(void)
{
    struct ofproto_sai *ofproto = xzalloc(sizeof *ofproto);

    SAI_API_TRACE_FN();

    return &ofproto->up;
}

static inline struct ofproto_sai *
ofproto_sai_cast(const struct ofproto *ofproto)
{
    ovs_assert(ofproto->ofproto_class == &ofproto_sai_class);
    return CONTAINER_OF(ofproto, struct ofproto_sai, up);
}

static int
ofproto_sai_construct(struct ofproto *ofproto_)
{
    struct ofproto_sai *ofproto = ofproto_sai_cast(ofproto_);
    int error = 0;

    SAI_API_TRACE_FN();

    VLOG_DBG("constructing ofproto - %s type - %s", ofproto->up.name,
             ofproto->up.type);

    sset_init(&ofproto->ports);
    sset_init(&ofproto->ghost_ports);
    ovs_mutex_init(&ofproto->mutex);
    /* Currently ACL is not supported. Implementation will be added in a
     * future. */
    ofproto_init_tables(ofproto_, 1);

    ovs_mutex_lock(&ofproto->mutex);
    hmap_init(&ofproto->bundles);
    hmap_insert(&all_ofproto_sai, &ofproto->all_ofproto_sai_node,
                hash_string(ofproto->up.name, 0));
    ovs_mutex_unlock(&ofproto->mutex);

    return error;
}

static void
ofproto_sai_destruct(struct ofproto *ofproto_ OVS_UNUSED)
{
    struct ofproto_sai *ofproto = ofproto_sai_cast(ofproto_);

    SAI_API_TRACE_FN();

    sset_destroy(&ofproto->ghost_ports);
    sset_destroy(&ofproto->ports);

    ovs_mutex_lock(&ofproto->mutex);
    hmap_remove(&all_ofproto_sai, &ofproto->all_ofproto_sai_node);
    ovs_mutex_unlock(&ofproto->mutex);
    ovs_mutex_destroy(&ofproto->mutex);
}

static void
ofproto_sai_dealloc(struct ofproto *ofproto_)
{
    struct ofproto_sai *ofproto = ofproto_sai_cast(ofproto_);

    SAI_API_TRACE_FN();

    free(ofproto);
}

static inline struct ofport_sai *
ofport_sai_cast(const struct ofport *ofport)
{
    return ofport ? CONTAINER_OF(ofport, struct ofport_sai, up) : NULL;
}

static struct ofport *
ofproto_sai_port_alloc(void)
{
    struct ofport_sai *port = xzalloc(sizeof *port);

    SAI_API_TRACE_FN();

    return &port->up;
}

static int
ofproto_sai_port_construct(struct ofport *port_)
{
    struct ofport_sai *port = ofport_sai_cast(port_);

    SAI_API_TRACE_FN();

    if (!strcmp(port->up.netdev->name, DEFAULT_BRIDGE_NAME)) {
        return 0;
    }

    port->port_oid = netdev_sai_oid_get(port->up.netdev);
    ofbundle_sai_port_pvid_vlan_del(port);

    return 0;
}

static void
ofproto_sai_port_destruct(struct ofport *port_ OVS_UNUSED)
{
    SAI_API_TRACE_FN();
}

static void
ofproto_sai_port_dealloc(struct ofport *port_)
{
    struct ofport_sai *port = ofport_sai_cast(port_);

    SAI_API_TRACE_FN();

    free(port);
}

static void
ofproto_sai_port_reconfigured(struct ofport *port_,
                              enum ofputil_port_config old_config)
{
    SAI_API_TRACE_FN();

    VLOG_DBG("port_reconfigured %p %d", port_, old_config);
}

static int
ofproto_sai_port_query_by_name(const struct ofproto *ofproto_,
                               const char *devname,
                               struct ofproto_port *ofproto_port)
{
    SAI_API_TRACE_FN();

    struct ofproto_sai *ofproto = ofproto_sai_cast(ofproto_);
    const char *type = netdev_get_type_from_name(devname);

    if (type) {
        const struct ofport *ofport;

        ofport = shash_find_data(&ofproto->up.port_by_name, devname);
        ofproto_port->ofp_port = ofport ? ofport->ofp_port : OFPP_NONE;
        ofproto_port->name = xstrdup(devname);
        ofproto_port->type = xstrdup(type);
        return 0;
    }

    return ENODEV;
}

static struct ofport_sai *
ofproto_sai_get_ofp_port(const struct ofproto_sai *ofproto,
                         ofp_port_t ofp_port)
{
    struct ofport *ofport = ofproto_get_port(&ofproto->up, ofp_port);

    return ofport ? ofport_sai_cast(ofport) : NULL;
}

static int
ofproto_sai_port_add(struct ofproto *ofproto_, struct netdev *netdev)
{
    struct ofproto_sai *ofproto = ofproto_sai_cast(ofproto_);

    SAI_API_TRACE_FN();

    sset_add(&ofproto->ports, netdev->name);
    return 0;
}

static int
ofproto_sai_port_del(struct ofproto *ofproto_, ofp_port_t ofp_port)
{
    struct ofproto_sai *ofproto = ofproto_sai_cast(ofproto_);
    struct ofport_sai *ofport = ofproto_sai_get_ofp_port(ofproto, ofp_port);

    SAI_API_TRACE_FN();

    sset_find_and_delete(&ofproto->ports,
                        netdev_get_name(ofport->up.netdev));
    return 0;
}

static int
ofproto_sai_port_get_stats(const struct ofport *ofport_,
                           struct netdev_stats *stats)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();

    return 0;
}

static int
ofproto_sai_port_dump_start(const struct ofproto *ofproto_ OVS_UNUSED,
                            void **statep)
{
    SAI_API_TRACE_FN();

    *statep = xzalloc(sizeof (struct port_dump_state));

    return 0;
}

static int
ofproto_sai_port_dump_next(const struct ofproto *ofproto_, void *state_,
                           struct ofproto_port *port)
{
    struct ofproto_sai *ofproto = ofproto_sai_cast(ofproto_);
    struct port_dump_state *state = state_;
    struct sset_node *node;

    SAI_API_TRACE_FN();

    if (state->has_port) {
        ofproto_port_destroy(&state->port);
        state->has_port = false;
    }

    while ((node = sset_at_position(&ofproto->ports, &state->bucket,
                                    &state->offset))) {
        int error;

        error = ofproto_sai_port_query_by_name(ofproto_, node->name,
                                               &state->port);
        if (!error) {
            *port = state->port;
            state->has_port = true;
            return 0;
        } else if (error != ENODEV) {
            return error;
        }
    }

    if (!state->ghost) {
        state->ghost = true;
        state->bucket = 0;
        state->offset = 0;
        return ofproto_sai_port_dump_next(ofproto_, state_, port);
    }

    return EOF;
}

static int
ofproto_sai_port_dump_done(const struct ofproto *ofproto_, void *state_)
{
    struct port_dump_state *state = state_;

    SAI_API_TRACE_FN();

    if (state->has_port) {
        ofproto_port_destroy(&state->port);
    }
    free(state);
    return 0;
}

static struct rule *
ofproto_sai_rule_alloc(void)
{
    struct rule *rule = xzalloc(sizeof *rule);

    SAI_API_TRACE_NOT_IMPLEMENTED_FN();

    return rule;
}

static void
ofproto_sai_rule_dealloc(struct rule *rule_)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();

    free(rule_);
}

static enum ofperr
ofproto_sai_rule_construct(struct rule *rule_)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();

    return 0;
}

static void
ofproto_sai_rule_insert(struct rule *rule_,
                        struct rule *old_rule,
                        bool forward_stats)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();
}

static void
ofproto_sai_rule_delete(struct rule *rule_)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();

    return;
}

static void
ofproto_sai_rule_destruct(struct rule *rule_)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();

    return;
}

static void
ofproto_sai_rule_get_stats(struct rule *rule_, uint64_t *packets,
                           uint64_t *bytes OVS_UNUSED,
                           long long int *used OVS_UNUSED)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();

    return;
}

static enum ofperr
ofproto_sai_rule_execute(struct rule *rule OVS_UNUSED, const struct flow *flow,
                         struct dp_packet *packet)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();

    return 0;
}

static bool
ofproto_sai_set_frag_handling(struct ofproto *ofproto_,
                              enum ofp_config_flags frag_handling)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();

    return false;
}

static enum ofperr
ofproto_sai_packet_out(struct ofproto *ofproto_, struct dp_packet *packet,
                       const struct flow *flow,
                       const struct ofpact *ofpacts, size_t ofpacts_len)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();

    return 0;
}

static sai_status_t
ofbundle_sai_port_vlan_set(const struct ofport_sai *port, sai_vlan_id_t vid,
                           sai_vlan_tagging_mode_t mode, bool add)
{
    sai_attribute_t attr = { };
    sai_vlan_port_t vlan_port = { };
    sai_status_t status = SAI_STATUS_SUCCESS;
    const struct sai_api_class *sai_api = sai_api_get_instance();

    if (NULL == port) {
        status = SAI_STATUS_INVALID_PARAMETER;
        SAI_ERROR_LOG_EXIT(status, "Failed to set vlan: got NULL port");
    }

    vlan_port.port_id = port->port_oid;
    vlan_port.tagging_mode = mode;
    if (add) {
        status = sai_api->vlan_api->add_ports_to_vlan(vid, 1, &vlan_port);
    } else {
        status = sai_api->vlan_api->remove_ports_from_vlan(vid, 1, &vlan_port);
    }
    SAI_ERROR_LOG_EXIT(status, "Failed to %s vlan %d on port %lu", add ? "add" : "remove", vid,
                       port->port_oid);

    if (add && (SAI_VLAN_PORT_UNTAGGED != mode)) {
        goto exit;
    }

    attr.id = SAI_PORT_ATTR_PORT_VLAN_ID;
    attr.value.u32 = vid;
    status = sai_api->port_api->set_port_attribute(port->port_oid, &attr);
    SAI_ERROR_LOG_EXIT(status, "Failed to set pvid %d for port %lu",
                       vid, port->port_oid);

exit:
    return status;
}

static sai_status_t
ofbundle_sai_port_vlan_add(const struct ofport_sai *port, sai_vlan_id_t vid,
                           sai_vlan_tagging_mode_t mode)
{
    return ofbundle_sai_port_vlan_set(port, vid, mode, true);
}

static sai_status_t
ofbundle_sai_port_vlan_del(const struct ofport_sai *port, sai_vlan_id_t vid)
{
    /* Mode doesn't matter when port is removed from vlan. */
    return ofbundle_sai_port_vlan_set(port, vid, SAI_VLAN_PORT_UNTAGGED,
                                      false);
}

static sai_status_t
ofbundle_sai_port_trunks_set(const struct ofport_sai *port,
                             unsigned long *trunks,
                             sai_vlan_tagging_mode_t mode, bool add)
{
    int vid = 0;
    sai_status_t status = SAI_STATUS_SUCCESS;

    if (NULL == trunks) {
        status = SAI_STATUS_INVALID_PARAMETER;
        SAI_ERROR_LOG_EXIT(status, "Got NULL trunks");
    }

    BITMAP_FOR_EACH_1(vid, VLAN_BITMAP_SIZE, trunks) {
        status = ofbundle_sai_port_vlan_set(port, vid, mode, add);
        SAI_ERROR_LOG_EXIT(status, "Failed to %s trunks", add ? "add" : "remove");
    }

exit:
    return status;
}

static sai_status_t
ofbundle_sai_port_trunks_add(struct ofport_sai *port, unsigned long *trunks,
                             sai_vlan_tagging_mode_t mode)
{
    return ofbundle_sai_port_trunks_set(port, trunks, mode, true);
}

static sai_status_t
ofbundle_sai_port_trunks_del(struct ofport_sai *port, unsigned long *trunks)
{
    return ofbundle_sai_port_trunks_set(port, trunks, SAI_VLAN_PORT_UNTAGGED,
                                        false);
}

static sai_vlan_tagging_mode_t
ofbundle_sai_vlan_mode(enum port_vlan_mode ovs_mode, bool is_trunk)
{
    sai_vlan_tagging_mode_t sai_mode;

    switch (ovs_mode) {
    case PORT_VLAN_ACCESS:
        sai_mode = SAI_VLAN_PORT_UNTAGGED;
        break;
    case PORT_VLAN_TRUNK:
    case PORT_VLAN_NATIVE_TAGGED:
        sai_mode = SAI_VLAN_PORT_TAGGED;
        break;
    case PORT_VLAN_NATIVE_UNTAGGED:
        sai_mode = is_trunk ? SAI_VLAN_PORT_TAGGED : SAI_VLAN_PORT_UNTAGGED;
        break;
    default:
        ovs_assert(false);
    }

    return sai_mode;
}

static sai_status_t
ofbundle_sai_port_pvid_vlan_del(struct ofport_sai *port)
{
    sai_attribute_t attr = { };
    sai_status_t status = SAI_STATUS_SUCCESS;
    const struct sai_api_class *sai_api = sai_api_get_instance();

    attr.id = SAI_PORT_ATTR_PORT_VLAN_ID;
    status = sai_api->port_api->get_port_attribute(port->port_oid, 1, &attr);
    SAI_ERROR_LOG_EXIT(status, "Failed to get pvid for port %lu",
                       port->port_oid);

    status = ofbundle_sai_port_vlan_del(port, attr.value.u32);
exit:
    return status;
}

static sai_status_t
ofbundle_sai_port_add(struct ofbundle_sai *bundle, struct ofport_sai *port)
{
    sai_status_t status = SAI_STATUS_SUCCESS;

    if (NULL == port) {
        status = SAI_STATUS_INVALID_PARAMETER;
        SAI_ERROR_LOG_EXIT(status, "Got NULL port to add");
    }

    /* Port belongs to other bundle - remove. */
    if (NULL != port->bundle) {
        ofproto_sai_bundle_remove(&port->up);
    }

    port->bundle = bundle;
    list_push_back(&bundle->ports, &port->bundle_node);

    if (-1 != bundle->vlan) {
        status = ofbundle_sai_port_vlan_add(port, bundle->vlan,
                                            ofbundle_sai_vlan_mode(bundle->vlan_mode, false));
        SAI_ERROR_LOG_EXIT(status, "Failed to add port to bundle");
    }

    if (NULL != bundle->trunks) {
        status = ofbundle_sai_port_trunks_add(port, bundle->trunks,
                                              ofbundle_sai_vlan_mode(bundle->vlan_mode, true));
        SAI_ERROR_LOG_EXIT(status, "Failed to add port to bundle");
    }

exit:
    return status;
}

static sai_status_t
ofbundle_sai_port_del(struct ofport_sai *port)
{
    struct ofbundle_sai *bundle;
    sai_status_t status = SAI_STATUS_SUCCESS;

    if (NULL == port) {
        status = SAI_STATUS_INVALID_PARAMETER;
        SAI_ERROR_LOG_EXIT(status, "Got NULL port to remove");
    }

    bundle = port->bundle;

    if (-1 != bundle->vlan) {
        status = ofbundle_sai_port_vlan_del(port, bundle->vlan);
        SAI_ERROR_LOG_EXIT(status, "Failed to remove port from bundle");
    }

    if (NULL != bundle->trunks) {
        status = ofbundle_sai_port_trunks_del(port, bundle->trunks);
        SAI_ERROR_LOG_EXIT(status, "Failed to remove port from bundle");
    }

exit:
    list_remove(&port->bundle_node);
    port->bundle = NULL;

    return status;
}

static void
ofbundle_sai_trunks_realloc(struct ofbundle_sai *bundle,
                            const unsigned long *trunks)
{
    /* Name didn't change. */
    if (vlan_bitmap_equal(bundle->trunks, trunks)) {
        return;
    }
    /* Free old name if there was any. */
    if (NULL != bundle->trunks) {
        free(bundle->trunks);
        bundle->trunks = NULL;
    }
    /* Copy new name if there is any. */
    if (NULL != trunks) {
        bundle->trunks = vlan_bitmap_clone(trunks);
    }
}

static sai_status_t
ofbundle_sai_vlan_reconfigure(struct ofbundle_sai *bundle,
                              const struct ofproto_bundle_settings *s)
{
    sai_status_t status = SAI_STATUS_SUCCESS;
    bool tag_changed = bundle->vlan != s->vlan;
    bool mod_changed = bundle->vlan_mode != s->vlan_mode;
    struct ofport_sai *port = NULL, *next_port = NULL;
    static unsigned long added_trunks[BITMAP_N_LONGS(VLAN_BITMAP_SIZE)];
    static unsigned long common_trunks[BITMAP_N_LONGS(VLAN_BITMAP_SIZE)];
    static unsigned long removed_trunks[BITMAP_N_LONGS(VLAN_BITMAP_SIZE)];
    static const unsigned long empty_trunks[BITMAP_N_LONGS(VLAN_BITMAP_SIZE)];

    /* Initialize all trunks as empty. */
    bitmap_and(added_trunks, empty_trunks, VLAN_BITMAP_SIZE);
    bitmap_and(common_trunks, empty_trunks, VLAN_BITMAP_SIZE);
    bitmap_and(removed_trunks, empty_trunks, VLAN_BITMAP_SIZE);
    /* Copy trunks from settings and bundle. */
    if (NULL != s->trunks) {
        bitmap_or(added_trunks, s->trunks, VLAN_BITMAP_SIZE);
    }
    if (NULL != bundle->trunks) {
        bitmap_or(removed_trunks, bundle->trunks, VLAN_BITMAP_SIZE);
    }
    bitmap_or(common_trunks, added_trunks, VLAN_BITMAP_SIZE);
    /* Figure out which trunks were added, removed, and which didn't change. */
    bitmap_and(common_trunks, removed_trunks, VLAN_BITMAP_SIZE);
    bitmap_not(removed_trunks, VLAN_BITMAP_SIZE);
    bitmap_or(removed_trunks, common_trunks, VLAN_BITMAP_SIZE);
    bitmap_not(removed_trunks, VLAN_BITMAP_SIZE);
    bitmap_not(added_trunks, VLAN_BITMAP_SIZE);
    bitmap_or(added_trunks, common_trunks, VLAN_BITMAP_SIZE);
    bitmap_not(added_trunks, VLAN_BITMAP_SIZE);

    /* Remove all ports from deleted vlans. */
    switch (bundle->vlan_mode) {
    case PORT_VLAN_ACCESS:
        if (tag_changed) {
            LIST_FOR_EACH_SAFE(port, next_port, bundle_node, &bundle->ports) {
                status = ofbundle_sai_port_vlan_del(port, bundle->vlan);
                SAI_ERROR_LOG_EXIT(status, "Failed to remove reconfigure vlans");
            }
        }
        break;
    case PORT_VLAN_TRUNK:
        LIST_FOR_EACH_SAFE(port, next_port, bundle_node, &bundle->ports) {
            status = ofbundle_sai_port_trunks_del(port, removed_trunks);
            SAI_ERROR_LOG_EXIT(status, "Failed to remove reconfigure vlans");
        }
        break;
    case PORT_VLAN_NATIVE_TAGGED:
    case PORT_VLAN_NATIVE_UNTAGGED:
        LIST_FOR_EACH_SAFE(port, next_port, bundle_node, &bundle->ports) {
            if (tag_changed || mod_changed) {
                status = ofbundle_sai_port_vlan_del(port, bundle->vlan);
                SAI_ERROR_LOG_EXIT(status, "Failed to remove reconfigure vlans");
            }
            status = ofbundle_sai_port_trunks_del(port, removed_trunks);
            SAI_ERROR_LOG_EXIT(status, "Failed to remove reconfigure vlans");
        }
        break;
    default:
        ovs_assert(false);
    }

    /* Add all ports to new vlans. */
    switch (s->vlan_mode) {
    case PORT_VLAN_ACCESS:
        if (tag_changed) {
            LIST_FOR_EACH_SAFE(port, next_port, bundle_node, &bundle->ports) {
                status = ofbundle_sai_port_vlan_add(port, s->vlan,
                                                   ofbundle_sai_vlan_mode(s->vlan_mode, false));
                SAI_ERROR_LOG_EXIT(status, "Failed to reconfigure vlans");
            }
        }
        break;
    case PORT_VLAN_TRUNK:
        LIST_FOR_EACH_SAFE(port, next_port, bundle_node, &bundle->ports) {
            status = ofbundle_sai_port_trunks_add(port, added_trunks,
                                                  ofbundle_sai_vlan_mode(s->vlan_mode, true));
            SAI_ERROR_LOG_EXIT(status, "Failed to reconfigure vlans");
        }
        break;
    case PORT_VLAN_NATIVE_TAGGED:
    case PORT_VLAN_NATIVE_UNTAGGED:
        LIST_FOR_EACH_SAFE(port, next_port, bundle_node, &bundle->ports) {
            if (tag_changed || mod_changed) {
                status = ofbundle_sai_port_vlan_add(port, s->vlan,
                                                    ofbundle_sai_vlan_mode(s->vlan_mode, false));
                SAI_ERROR_LOG_EXIT(status, "Failed to reconfigure vlans");

            }
            status = ofbundle_sai_port_trunks_add(port, added_trunks,
                                                  ofbundle_sai_vlan_mode(s->vlan_mode, true));
            SAI_ERROR_LOG_EXIT(status, "Failed to reconfigure vlans");
        }
        break;
    default:
        ovs_assert(false);
    }

    bundle->vlan = s->vlan;
    bundle->vlan_mode = s->vlan_mode;
    ofbundle_sai_trunks_realloc(bundle, s->trunks);

exit:
    return status;
}

static sai_status_t
ofbundle_sai_ports_reconfigure(struct ofbundle_sai *bundle,
                               const struct ofproto_bundle_settings *s)
{
    size_t i;
    bool port_found = false;
    sai_status_t status = SAI_STATUS_SUCCESS;
    struct ofport_sai *port = NULL, *next_port = NULL, *s_port = NULL;

    /* Figure out which ports were removed. */
    LIST_FOR_EACH_SAFE(port, next_port, bundle_node, &bundle->ports) {
        port_found = false;
        for (i = 0; i < s->n_slaves; i++) {
            s_port = ofproto_sai_get_ofp_port(bundle->ofproto, s->slaves[i]);
            if (port == s_port) {
                port_found = true;
                break;
            }
        }
        if (!port_found) {
            status = ofbundle_sai_port_del(port);
            SAI_ERROR_LOG_EXIT(status, "Failed to reconfigure ports");
        }
    }

    status = ofbundle_sai_vlan_reconfigure(bundle, s);
    SAI_ERROR_LOG_EXIT(status, "Failed to reconfigure ports");

    /* Figure out which ports were added. */
    port = NULL;
    next_port = NULL;
    for (i = 0; i < s->n_slaves; i++) {
        port_found = false;
        s_port = ofproto_sai_get_ofp_port(bundle->ofproto, s->slaves[i]);

        LIST_FOR_EACH_SAFE(port, next_port, bundle_node, &bundle->ports) {
            if (port == s_port) {
                port_found = true;
                break;
            }
        }

        if (!port_found) {
            status = ofbundle_sai_port_add(bundle, s_port);
        }
        SAI_ERROR_LOG_EXIT(status, "Failed to reconfigure ports");
    }

exit:
    return status;
}

static void
ofbundle_sai_rename(struct ofbundle_sai *bundle, const char *name)
{
    /* Name didn't change. */
    if ((NULL != bundle->name) && (NULL != name)
        && !strcmp(name, bundle->name)) {
        return;
    }
    /* Free old name if there was any. */
    if (NULL != bundle->name) {
        free(bundle->name);
        bundle->name = NULL;
    }
    /* Copy new name if there is any. */
    if (NULL != name) {
        bundle->name = xstrdup(name);
    }
}

static struct ofbundle_sai *
ofbundle_sai_create(struct ofproto_sai *ofproto, void *aux,
                    const struct ofproto_bundle_settings *s)
{
    struct ofbundle_sai *bundle = xzalloc(sizeof (struct ofbundle_sai));

    hmap_insert(&ofproto->bundles, &bundle->hmap_node, hash_pointer(aux, 0));
    list_init(&bundle->ports);
    ofbundle_sai_rename(bundle, s->name);
    ofbundle_sai_trunks_realloc(bundle, s->trunks);

    bundle->ofproto = ofproto;
    bundle->aux = aux;

    return bundle;
}

static void
ofbundle_sai_destroy(struct ofbundle_sai *bundle)
{
    sai_status_t status = SAI_STATUS_SUCCESS;
    struct ofport_sai *port = NULL, *next_port = NULL;

    if (NULL == bundle) {
        return;
    }

    LIST_FOR_EACH_SAFE(port, next_port, bundle_node, &bundle->ports) {
        status = ofbundle_sai_port_del(port);
        SAI_ERROR_LOG_EXIT(status, "Failed to destroy bundle");
    }

exit:
    ofbundle_sai_rename(bundle, NULL);
    ofbundle_sai_trunks_realloc(bundle, NULL);
    hmap_remove(&bundle->ofproto->bundles, &bundle->hmap_node);

    free(bundle);
}

static struct ofbundle_sai *
ofbundle_sai_lookup(struct ofproto_sai *ofproto, void *aux)
{
    struct ofbundle_sai *bundle;

    HMAP_FOR_EACH_IN_BUCKET(bundle, hmap_node, hash_pointer(aux, 0),
                            &ofproto->bundles) {
        if (bundle->aux == aux) {
            return bundle;
        }
    }

    return NULL;
}

static int
ofproto_sai_bundle_set(struct ofproto *ofproto_, void *aux,
                       const struct ofproto_bundle_settings *s)
{
    struct ofbundle_sai *bundle = NULL;
    sai_status_t status = SAI_STATUS_SUCCESS;
    struct ofproto_sai *ofproto = ofproto_sai_cast(ofproto_);

    SAI_API_TRACE_FN();

    if ((s && !strcmp(s->name, DEFAULT_BRIDGE_NAME)) ||
        strcmp(ofproto_->type, SAI_INTERFACE_TYPE_SYSTEM)) {
        goto exit;
    }

    bundle = ofbundle_sai_lookup(ofproto, aux);
    if (NULL == s) {
        ofbundle_sai_destroy(bundle);
        goto exit;
    }

    if (NULL == bundle) {
        bundle = ofbundle_sai_create(ofproto, aux, s);
    }

    status = ofbundle_sai_ports_reconfigure(bundle, s);
    SAI_ERROR_LOG_EXIT(status, "Failed to set bundle");

exit:
    return SAI_ERROR(status);
}

static void
ofproto_sai_bundle_remove(struct ofport *port_)
{
    sai_status_t status = SAI_STATUS_SUCCESS;
    struct ofport_sai *port = ofport_sai_cast(port_);
    struct ofbundle_sai *bundle = port->bundle;

    SAI_API_TRACE_FN();

    if (NULL == bundle) {
        return;
    }

    status = ofbundle_sai_port_del(port);
    SAI_ERROR_LOG_EXIT(status, "Failed to remove bundle");

exit:
    if (list_is_empty(&bundle->ports)) {
        ofbundle_sai_destroy(bundle);
    }
}

static int
ofproto_sai_bundle_get(struct ofproto *ofproto_, void *aux, int *bundle_handle)
{
    SAI_API_TRACE_FN();

    return 0;
}

static int
ofproto_sai_set_vlan(struct ofproto *ofproto, int vid, bool add)
{
    sai_status_t status = SAI_STATUS_SUCCESS;
    const struct sai_api_class *sai_api = sai_api_get_instance();

    SAI_API_TRACE_FN();

    if (add) {
        status = sai_api->vlan_api->create_vlan(vid);
    } else {
        status = sai_api->vlan_api->remove_vlan(vid);
    }
    SAI_ERROR_LOG_EXIT(status, "Failed to %s vlan vid %d", add ? "create" : "remove", vid);

exit:
    return SAI_ERROR(status);
}

static inline struct ofproto_sai_group *
ofproto_sai_group_cast(const struct ofgroup *group)
{
    return group ? CONTAINER_OF(group, struct ofproto_sai_group, up) : NULL;
}

static struct ofgroup *
ofproto_sai_group_alloc(void)
{
    struct ofproto_sai_group *group = xzalloc(sizeof *group);

    SAI_API_TRACE_NOT_IMPLEMENTED_FN();

    return &group->up;
}

static enum ofperr
ofproto_sai_group_construct(struct ofgroup *group_)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();

    return 0;
}

static void
ofproto_sai_group_destruct(struct ofgroup *group_)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();

    return;
}

static void
ofproto_sai_group_dealloc(struct ofgroup *group_)
{
    struct ofproto_sai_group *group = ofproto_sai_group_cast(group_);

    SAI_API_TRACE_NOT_IMPLEMENTED_FN();

    free(group);
}

static enum ofperr
ofproto_sai_group_modify(struct ofgroup *group_)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();

    return 0;
}

static enum ofperr
ofproto_sai_group_get_stats(const struct ofgroup *group_,
                            struct ofputil_group_stats *ogs)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();

    return 0;
}

static const char *
ofproto_sai_get_datapath_version(const struct ofproto *ofproto_ OVS_UNUSED)
{
    SAI_API_TRACE_FN();

    return strdup(SAI_DATAPATH_VERSION);
}

static int
ofproto_sai_add_l3_host_entry(const struct ofproto *ofproto_, void *aux,
                              bool is_ipv6_addr, char *ip_addr,
                              char *next_hop_mac_addr, int *l3_egress_id)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();

    return 0;
}

static int
ofproto_sai_delete_l3_host_entry(const struct ofproto *ofproto_, void *aux,
                                 bool is_ipv6_addr, char *ip_addr,
                                 int *l3_egress_id)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();

    return 0;
}

static int
ofproto_sai_get_l3_host_hit_bit(const struct ofproto *ofproto_, void *aux,
                                bool is_ipv6_addr, char *ip_addr,
                                bool *hit_bit)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();

    return 0;
}

static int
ofproto_sai_l3_route_action(const struct ofproto *ofprotop,
                            enum ofproto_route_action action,
                            struct ofproto_route *routep)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();

    return 0;
}

static int
ofproto_sai_l3_ecmp_set(const struct ofproto *ofprotop, bool enable)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();

    return 0;
}

static int
ofproto_sai_l3_ecmp_hash_set(const struct ofproto *ofprotop, unsigned int hash,
                             bool enable)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();

    return 0;
}

static int
ofproto_sai_run(struct ofproto *ofproto)
{
    SAI_API_TRACE_FN();

    return 0;
}

static void
ofproto_sai_wait(struct ofproto *ofproto)
{
    SAI_API_TRACE_FN();
}

static void
ofproto_sai_set_tables_version(struct ofproto *ofproto, cls_version_t version)
{
    SAI_API_TRACE_FN();

    return;
}
