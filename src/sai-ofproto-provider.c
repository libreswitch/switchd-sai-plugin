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

#include <vswitch-idl.h>
#include <openswitch-idl.h>

#include <sai-common.h>
#include <sai-netdev.h>
#include <sai-api-class.h>
#include <sai-log.h>
#include <sai-port.h>
#include <sai-vlan.h>
#include <sai-host-intf.h>

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

static const unsigned long empty_trunks[BITMAP_N_LONGS(VLAN_BITMAP_SIZE)];

static void __init(const struct shash *);
static void __enumerate_types(struct sset *);
static int __enumerate_names(const char *, struct sset *);
static int __del(const char *, const char *);
static const char *__port_open_type(const char *, const char *);
static struct ofproto *__alloc(void);
static inline struct ofproto_sai *__ofproto_sai_cast(const struct ofproto *);
static int __construct(struct ofproto *);
static void __destruct(struct ofproto *);
static void __sai_dealloc(struct ofproto *);
static inline struct ofport_sai *__ofport_sai_cast(const struct ofport *);
static struct ofport *__port_alloc(void);
static int __port_construct(struct ofport *);
static void __port_destruct(struct ofport *);
static void __port_dealloc(struct ofport *);
static void __port_reconfigured(struct ofport *, enum ofputil_port_config);
static int __port_query_by_name(const struct ofproto *, const char *,
                                struct ofproto_port *);
static struct ofport_sai *__get_ofp_port(const struct ofproto_sai *,
                                         ofp_port_t);
static int __port_add(struct ofproto *, struct netdev *netdev);
static int __port_del(struct ofproto *, ofp_port_t);
static int __port_get_stats(const struct ofport *, struct netdev_stats *);
static int __port_dump_start(const struct ofproto *, void **);
static int __port_dump_next(const struct ofproto *, void *,
                            struct ofproto_port *);
static int __port_dump_done(const struct ofproto *, void *);
static struct rule *__rule_alloc(void);
static void __rule_dealloc(struct rule *);
static enum ofperr __rule_construct(struct rule *);
static void __rule_insert(struct rule *, struct rule *, bool);
static void __rule_delete(struct rule *);
static void __rule_destruct(struct rule *);
static void __rule_get_stats(struct rule *, uint64_t *, uint64_t *,
                             long long int *);
static enum ofperr __rule_execute(struct rule *, const struct flow *,
                                  struct dp_packet *);
static bool __set_frag_handling(struct ofproto *, enum ofp_config_flags);
static enum ofperr __packet_out(struct ofproto *, struct dp_packet *,
                                const struct flow *, const struct ofpact *,
                                size_t);
static int __ofbundle_port_add(struct ofbundle_sai *, struct ofport_sai *);
static int __ofbundle_port_del(struct ofport_sai *);
static void __trunks_realloc(struct ofbundle_sai *, const unsigned long *);
static int __native_tagged_vlan_set(int, uint32_t, bool);
static int __vlan_reconfigure(struct ofbundle_sai *,
                              const struct ofproto_bundle_settings *);
static int __ofbundle_ports_reconfigure(struct ofbundle_sai *,
                                        const struct
                                        ofproto_bundle_settings *);
static void __ofbundle_rename(struct ofbundle_sai *, const char *);
static struct ofbundle_sai *__ofbundle_create(struct ofproto_sai *, void *,
                                              const struct
                                              ofproto_bundle_settings *);
static void __ofbundle_destroy(struct ofbundle_sai *);
static struct ofbundle_sai *__ofbundle_lookup(struct ofproto_sai *, void *);
static int __bundle_set(struct ofproto *, void *,
                                  const struct ofproto_bundle_settings *);
static void __bundle_remove(struct ofport *);
static int __bundle_get(struct ofproto *, void *, int *);
static int __set_vlan(struct ofproto *, int, bool);
static inline struct ofproto_sai_group *__ofproto_sai_group_cast(const struct
                                                                 ofgroup *);
static struct ofgroup *__group_alloc(void);
static enum ofperr __group_construct(struct ofgroup *);
static void __group_destruct(struct ofgroup *);
static void __group_dealloc(struct ofgroup *);
static enum ofperr __group_modify(struct ofgroup *);
static enum ofperr __group_get_stats(const struct ofgroup *,
                                     struct ofputil_group_stats *);
static const char *__get_datapath_version(const struct ofproto *);
static int __add_l3_host_entry(const struct ofproto *, void *, bool, char *,
                               char *, int *);
static int __delete_l3_host_entry(const struct ofproto *, void *, bool, char *,
                                  int *);
static int __get_l3_host_hit_bit(const struct ofproto *, void *, bool, char *,
                                 bool *);
static int __l3_route_action(const struct ofproto *, enum ofproto_route_action,
                             struct ofproto_route *);
static int __l3_ecmp_set(const struct ofproto *, bool);
static int __l3_ecmp_hash_set(const struct ofproto *, unsigned int, bool);
static int __run(struct ofproto *);
static void __wait(struct ofproto *);
static void __set_tables_version(struct ofproto *, cls_version_t);

const struct ofproto_class ofproto_sai_class = {
    PROVIDER_INIT_GENERIC(init,                  __init)
    PROVIDER_INIT_GENERIC(enumerate_types,       __enumerate_types)
    PROVIDER_INIT_GENERIC(enumerate_names,       __enumerate_names)
    PROVIDER_INIT_GENERIC(del,                   __del)
    PROVIDER_INIT_GENERIC(port_open_type,        __port_open_type)
    PROVIDER_INIT_GENERIC(type_run,              NULL)
    PROVIDER_INIT_GENERIC(type_wait,             NULL)
    PROVIDER_INIT_GENERIC(alloc,                 __alloc)
    PROVIDER_INIT_GENERIC(construct,             __construct)
    PROVIDER_INIT_GENERIC(destruct,              __destruct)
    PROVIDER_INIT_GENERIC(dealloc,               __sai_dealloc)
    PROVIDER_INIT_GENERIC(run,                   __run)
    PROVIDER_INIT_GENERIC(wait,                  __wait)
    PROVIDER_INIT_GENERIC(get_memory_usage,      NULL)
    PROVIDER_INIT_GENERIC(type_get_memory_usage, NULL)
    PROVIDER_INIT_GENERIC(flush,                 NULL)
    PROVIDER_INIT_GENERIC(query_tables,          NULL)
    PROVIDER_INIT_GENERIC(set_tables_version,    __set_tables_version)
    PROVIDER_INIT_GENERIC(port_alloc,            __port_alloc)
    PROVIDER_INIT_GENERIC(port_construct,        __port_construct)
    PROVIDER_INIT_GENERIC(port_destruct,         __port_destruct)
    PROVIDER_INIT_GENERIC(port_dealloc,          __port_dealloc)
    PROVIDER_INIT_GENERIC(port_modified,         NULL)
    PROVIDER_INIT_GENERIC(port_reconfigured,     __port_reconfigured)
    PROVIDER_INIT_GENERIC(port_query_by_name,    __port_query_by_name)
    PROVIDER_INIT_GENERIC(port_add,              __port_add)
    PROVIDER_INIT_GENERIC(port_del,              __port_del)
    PROVIDER_INIT_GENERIC(port_get_stats,        __port_get_stats)
    PROVIDER_INIT_GENERIC(port_dump_start,       __port_dump_start)
    PROVIDER_INIT_GENERIC(port_dump_next,        __port_dump_next)
    PROVIDER_INIT_GENERIC(port_dump_done,        __port_dump_done)
    PROVIDER_INIT_GENERIC(port_poll,             NULL)
    PROVIDER_INIT_GENERIC(port_poll_wait,        NULL)
    PROVIDER_INIT_GENERIC(port_is_lacp_current,  NULL)
    PROVIDER_INIT_GENERIC(port_get_lacp_stats,   NULL)
    PROVIDER_INIT_GENERIC(rule_construct,        NULL)
    PROVIDER_INIT_GENERIC(rule_alloc,            __rule_alloc)
    PROVIDER_INIT_GENERIC(rule_construct,        __rule_construct)
    PROVIDER_INIT_GENERIC(rule_insert,           __rule_insert)
    PROVIDER_INIT_GENERIC(rule_delete,           __rule_delete)
    PROVIDER_INIT_GENERIC(rule_destruct,         __rule_destruct)
    PROVIDER_INIT_GENERIC(rule_dealloc,          __rule_dealloc)
    PROVIDER_INIT_GENERIC(rule_get_stats,        __rule_get_stats)
    PROVIDER_INIT_GENERIC(rule_execute,          __rule_execute)
    PROVIDER_INIT_GENERIC(set_frag_handling,     __set_frag_handling)
    PROVIDER_INIT_GENERIC(packet_out,            __packet_out)
    PROVIDER_INIT_GENERIC(set_netflow,           NULL)
    PROVIDER_INIT_GENERIC(get_netflow_ids,       NULL)
    PROVIDER_INIT_GENERIC(set_sflow,             NULL)
    PROVIDER_INIT_GENERIC(set_ipfix,             NULL)
    PROVIDER_INIT_GENERIC(set_cfm,               NULL)
    PROVIDER_INIT_GENERIC(cfm_status_changed,    NULL)
    PROVIDER_INIT_GENERIC(get_cfm_status,        NULL)
    PROVIDER_INIT_GENERIC(set_bfd,               NULL)
    PROVIDER_INIT_GENERIC(bfd_status_changed,    NULL)
    PROVIDER_INIT_GENERIC(get_bfd_status,        NULL)
    PROVIDER_INIT_GENERIC(set_stp,               NULL)
    PROVIDER_INIT_GENERIC(get_stp_status,        NULL)
    PROVIDER_INIT_GENERIC(set_stp_port,          NULL)
    PROVIDER_INIT_GENERIC(get_stp_port_status,   NULL)
    PROVIDER_INIT_GENERIC(get_stp_port_stats,    NULL)
    PROVIDER_INIT_GENERIC(set_rstp,              NULL)
    PROVIDER_INIT_GENERIC(get_rstp_status,       NULL)
    PROVIDER_INIT_GENERIC(set_rstp_port,         NULL)
    PROVIDER_INIT_GENERIC(get_rstp_port_status,  NULL)
    PROVIDER_INIT_GENERIC(set_queues,            NULL)
    PROVIDER_INIT_GENERIC(bundle_set,            __bundle_set)
    PROVIDER_INIT_GENERIC(bundle_remove,         __bundle_remove)
    PROVIDER_INIT_OPS_SPECIFIC(bundle_get,       __bundle_get)
    PROVIDER_INIT_OPS_SPECIFIC(set_vlan,         __set_vlan)
    PROVIDER_INIT_GENERIC(mirror_set,            NULL)
    PROVIDER_INIT_GENERIC(mirror_get_stats,      NULL)
    PROVIDER_INIT_GENERIC(set_flood_vlans,       NULL)
    PROVIDER_INIT_GENERIC(is_mirror_output_bundle, NULL)
    PROVIDER_INIT_GENERIC(forward_bpdu_changed,  NULL)
    PROVIDER_INIT_GENERIC(set_mac_table_config,  NULL)
    PROVIDER_INIT_GENERIC(set_mcast_snooping,    NULL)
    PROVIDER_INIT_GENERIC(set_mcast_snooping_port, NULL)
    PROVIDER_INIT_GENERIC(set_realdev,           NULL)
    PROVIDER_INIT_GENERIC(meter_get_features,    NULL)
    PROVIDER_INIT_GENERIC(meter_set,             NULL)
    PROVIDER_INIT_GENERIC(meter_get,             NULL)
    PROVIDER_INIT_GENERIC(meter_del,             NULL)
    PROVIDER_INIT_GENERIC(group_alloc,           __group_alloc)
    PROVIDER_INIT_GENERIC(group_construct,       __group_construct)
    PROVIDER_INIT_GENERIC(group_destruct,        __group_destruct)
    PROVIDER_INIT_GENERIC(group_dealloc,         __group_dealloc)
    PROVIDER_INIT_GENERIC(group_modify,          __group_modify)
    PROVIDER_INIT_GENERIC(group_get_stats,       __group_get_stats)
    PROVIDER_INIT_GENERIC(get_datapath_version,  __get_datapath_version)
    PROVIDER_INIT_OPS_SPECIFIC(add_l3_host_entry, __add_l3_host_entry)
    PROVIDER_INIT_OPS_SPECIFIC(delete_l3_host_entry, __delete_l3_host_entry)
    PROVIDER_INIT_OPS_SPECIFIC(get_l3_host_hit,  __get_l3_host_hit_bit)
    PROVIDER_INIT_OPS_SPECIFIC(l3_route_action,  __l3_route_action)
    PROVIDER_INIT_OPS_SPECIFIC(l3_ecmp_set,      __l3_ecmp_set)
    PROVIDER_INIT_OPS_SPECIFIC(l3_ecmp_hash_set, __l3_ecmp_hash_set)
};


/**
 * Regster ofproto provider.
 */
void
ofproto_sai_register(void)
{
    ofproto_class_register(&ofproto_sai_class);
}

static void
__init(const struct shash *iface_hints)
{
    SAI_API_TRACE_FN();

    ops_sai_api_init();
    ops_sai_hostint_traps_register();
}

static void
__enumerate_types(struct sset *types)
{
    SAI_API_TRACE_FN();

    /* VRF isn't supported yet, but needed for ops-switchd. */
    sset_add(types, SAI_INTERFACE_TYPE_VRF);
    sset_add(types, SAI_INTERFACE_TYPE_SYSTEM);
}

static int
__enumerate_names(const char *type, struct sset *names)
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
__del(const char *type, const char *name)
{
    SAI_API_TRACE_FN();

    ops_sai_api_uninit();

    return 0;
}

static const char *
__port_open_type(const char *datapath_type, const char *port_type)
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
__alloc(void)
{
    struct ofproto_sai *ofproto = xzalloc(sizeof *ofproto);

    SAI_API_TRACE_FN();

    return &ofproto->up;
}

/*
 * Cast netdev ofproto to ofproto_sai.
 */
static inline struct ofproto_sai *
__ofproto_sai_cast(const struct ofproto *ofproto)
{
    ovs_assert(ofproto->ofproto_class == &ofproto_sai_class);
    return CONTAINER_OF(ofproto, struct ofproto_sai, up);
}

static int
__construct(struct ofproto *ofproto_)
{
    struct ofproto_sai *ofproto = __ofproto_sai_cast(ofproto_);
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
__destruct(struct ofproto *ofproto_ OVS_UNUSED)
{
    struct ofproto_sai *ofproto = __ofproto_sai_cast(ofproto_);

    SAI_API_TRACE_FN();

    sset_destroy(&ofproto->ghost_ports);
    sset_destroy(&ofproto->ports);

    ovs_mutex_lock(&ofproto->mutex);
    hmap_remove(&all_ofproto_sai, &ofproto->all_ofproto_sai_node);
    ovs_mutex_unlock(&ofproto->mutex);
    ovs_mutex_destroy(&ofproto->mutex);
}

static void
__sai_dealloc(struct ofproto *ofproto_)
{
    struct ofproto_sai *ofproto = __ofproto_sai_cast(ofproto_);

    SAI_API_TRACE_FN();

    free(ofproto);
}

static inline struct ofport_sai *
__ofport_sai_cast(const struct ofport *ofport)
{
    return ofport ? CONTAINER_OF(ofport, struct ofport_sai, up) : NULL;
}

static struct ofport *
__port_alloc(void)
{
    struct ofport_sai *port = xzalloc(sizeof *port);

    SAI_API_TRACE_FN();

    return &port->up;
}

static int
__port_construct(struct ofport *port_)
{
    SAI_API_TRACE_FN();

    return 0;
}

static void
__port_destruct(struct ofport *port_ OVS_UNUSED)
{
    SAI_API_TRACE_FN();
}

static void
__port_dealloc(struct ofport *port_)
{
    struct ofport_sai *port = __ofport_sai_cast(port_);

    SAI_API_TRACE_FN();

    free(port);
}

static void
__port_reconfigured(struct ofport *port_, enum ofputil_port_config old_config)
{
    SAI_API_TRACE_FN();

    VLOG_DBG("port_reconfigured %p %d", port_, old_config);
}

static int
__port_query_by_name(const struct ofproto *ofproto_, const char *devname,
                     struct ofproto_port *ofproto_port)
{
    SAI_API_TRACE_FN();

    struct ofproto_sai *ofproto = __ofproto_sai_cast(ofproto_);
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
__get_ofp_port(const struct ofproto_sai *ofproto, ofp_port_t ofp_port)
{
    struct ofport *ofport = ofproto_get_port(&ofproto->up, ofp_port);

    return ofport ? __ofport_sai_cast(ofport) : NULL;
}

static int
__port_add(struct ofproto *ofproto_, struct netdev *netdev)
{
    struct ofproto_sai *ofproto = __ofproto_sai_cast(ofproto_);

    SAI_API_TRACE_FN();

    sset_add(&ofproto->ports, netdev->name);
    return 0;
}

static int
__port_del(struct ofproto *ofproto_, ofp_port_t ofp_port)
{
    struct ofproto_sai *ofproto = __ofproto_sai_cast(ofproto_);
    struct ofport_sai *ofport = __get_ofp_port(ofproto, ofp_port);

    SAI_API_TRACE_FN();

    sset_find_and_delete(&ofproto->ports,
                        netdev_get_name(ofport->up.netdev));
    return 0;
}

static int
__port_get_stats(const struct ofport *ofport_, struct netdev_stats *stats)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();

    return 0;
}

static int
__port_dump_start(const struct ofproto *ofproto_ OVS_UNUSED,
                            void **statep)
{
    SAI_API_TRACE_FN();

    *statep = xzalloc(sizeof (struct port_dump_state));

    return 0;
}

static int
__port_dump_next(const struct ofproto *ofproto_, void *state_,
                 struct ofproto_port *port)
{
    struct ofproto_sai *ofproto = __ofproto_sai_cast(ofproto_);
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

        error = __port_query_by_name(ofproto_, node->name, &state->port);
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
        return __port_dump_next(ofproto_, state_, port);
    }

    return EOF;
}

static int
__port_dump_done(const struct ofproto *ofproto_, void *state_)
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
__rule_alloc(void)
{
    struct rule *rule = xzalloc(sizeof *rule);

    SAI_API_TRACE_NOT_IMPLEMENTED_FN();

    return rule;
}

static void
__rule_dealloc(struct rule *rule_)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();

    free(rule_);
}

static enum ofperr
__rule_construct(struct rule *rule_)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();

    return 0;
}

static void
__rule_insert(struct rule *rule_,
                        struct rule *old_rule,
                        bool forward_stats)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();
}

static void
__rule_delete(struct rule *rule_)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();

    return;
}

static void
__rule_destruct(struct rule *rule_)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();

    return;
}

static void
__rule_get_stats(struct rule *rule_, uint64_t *packets,
                           uint64_t *bytes OVS_UNUSED,
                           long long int *used OVS_UNUSED)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();

    return;
}

static enum ofperr
__rule_execute(struct rule *rule OVS_UNUSED, const struct flow *flow,
                         struct dp_packet *packet)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();

    return 0;
}

static bool
__set_frag_handling(struct ofproto *ofproto_,
                              enum ofp_config_flags frag_handling)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();

    return false;
}

static enum ofperr
__packet_out(struct ofproto *ofproto_, struct dp_packet *packet,
                       const struct flow *flow,
                       const struct ofpact *ofpacts, size_t ofpacts_len)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();

    return 0;
}

/*
 * Add port to bundle.
 */
static int
__ofbundle_port_add(struct ofbundle_sai *bundle, struct ofport_sai *port)
{
    int status = 0;
    uint32_t hw_id = netdev_sai_hw_id_get(port->up.netdev);

    if (NULL == port) {
        status = EINVAL;
        ERRNO_LOG_EXIT(status, "Got NULL port to add");
    }

    /* Port belongs to other bundle - remove. */
    if (NULL != port->bundle) {
        VLOG_WARN("Add port to bundle: removing port from old bundle");
        __bundle_remove(&port->up);
    }

    port->bundle = bundle;
    list_push_back(&bundle->ports, &port->bundle_node);

    if (-1 != bundle->vlan) {
        status = ops_sai_vlan_access_port_add(bundle->vlan, hw_id);
        ERRNO_LOG_EXIT(status, "Failed to add port to bundle");
    }

    if (NULL != bundle->trunks) {
        status = ops_sai_vlan_trunks_port_add(bundle->trunks, hw_id);
        ERRNO_LOG_EXIT(status, "Failed to add port to bundle");
    }

exit:
    return status;
}

/*
 * Remove port from bundle.
 */
static int
__ofbundle_port_del(struct ofport_sai *port)
{
    struct ofbundle_sai *bundle;
    int status = 0;
    uint32_t hw_id = netdev_sai_hw_id_get(port->up.netdev);

    if (NULL == port) {
        status = EINVAL;
        ERRNO_LOG_EXIT(status, "Got NULL port to remove");
    }

    bundle = port->bundle;

    if (-1 != bundle->vlan) {
        status = ops_sai_vlan_access_port_del(bundle->vlan, hw_id);
        ERRNO_LOG_EXIT(status, "Failed to remove port from bundle");
    }

    if (NULL != bundle->trunks) {
        status = ops_sai_vlan_trunks_port_del(bundle->trunks, hw_id);
        ERRNO_LOG_EXIT(status, "Failed to remove port from bundle");
    }

exit:
    list_remove(&port->bundle_node);
    port->bundle = NULL;

    return status;
}

/*
 * Reallocate bundle trunks.
 */
static void
__trunks_realloc(struct ofbundle_sai *bundle,
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

/*
 * Set native tagged vlan and corresponding pvid.
 */
static int __native_tagged_vlan_set(int vid, uint32_t hw_id, bool add)
{
    int status = 0;
    static unsigned long trunks[BITMAP_N_LONGS(VLAN_BITMAP_SIZE)];

    bitmap_and(trunks, empty_trunks, VLAN_BITMAP_SIZE);
    bitmap_set1(trunks, vid);

    status =  add ? ops_sai_vlan_trunks_port_add(trunks, hw_id) :
                    ops_sai_vlan_trunks_port_del(trunks, hw_id);
    ERRNO_EXIT(status);

    status = ops_sai_port_pvid_set(hw_id, add ? vid :
                                   OPS_SAI_PORT_DEFAULT_PVID);
    ERRNO_EXIT(status);

exit:
    return status;
}

/*
 * Reconfigure port to vlan settings. Remove ports from vlans that were in
 * bundle and add ports to vlan in new settings.
 */
static int
__vlan_reconfigure(struct ofbundle_sai *bundle,
                              const struct ofproto_bundle_settings *s)
{
    int status = 0;
    bool tag_changed = bundle->vlan != s->vlan;
    bool mod_changed = bundle->vlan_mode != s->vlan_mode;
    struct ofport_sai *port = NULL, *next_port = NULL;
    static unsigned long added_trunks[BITMAP_N_LONGS(VLAN_BITMAP_SIZE)];
    static unsigned long common_trunks[BITMAP_N_LONGS(VLAN_BITMAP_SIZE)];
    static unsigned long removed_trunks[BITMAP_N_LONGS(VLAN_BITMAP_SIZE)];

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
                status = ops_sai_vlan_access_port_del(bundle->vlan,
                                                      netdev_sai_hw_id_get(port->up.netdev));
                ERRNO_LOG_EXIT(status, "Failed to remove reconfigure vlans");
            }
        }
        break;

    case PORT_VLAN_TRUNK:
        LIST_FOR_EACH_SAFE(port, next_port, bundle_node, &bundle->ports) {
            status = ops_sai_vlan_trunks_port_del(removed_trunks,
                                                  netdev_sai_hw_id_get(port->up.netdev));
            ERRNO_LOG_EXIT(status, "Failed to remove reconfigure vlans");
        }
        break;

    case PORT_VLAN_NATIVE_UNTAGGED:
        LIST_FOR_EACH_SAFE(port, next_port, bundle_node, &bundle->ports) {
            if (tag_changed || mod_changed) {
                status = ops_sai_vlan_access_port_del(bundle->vlan,
                                                      netdev_sai_hw_id_get(port->up.netdev));
                ERRNO_LOG_EXIT(status, "Failed to remove reconfigure vlans");
            }
            status = ops_sai_vlan_trunks_port_del(removed_trunks,
                                                  netdev_sai_hw_id_get(port->up.netdev));
            ERRNO_LOG_EXIT(status, "Failed to remove reconfigure vlans");
        }
        break;

    case PORT_VLAN_NATIVE_TAGGED:
        LIST_FOR_EACH_SAFE(port, next_port, bundle_node, &bundle->ports) {
            if (tag_changed || mod_changed) {
                status = __native_tagged_vlan_set(bundle->vlan,
                                                  netdev_sai_hw_id_get(port->up.netdev),
                                                  false);
                ERRNO_LOG_EXIT(status, "Failed to remove reconfigure vlans");
            }
            status = ops_sai_vlan_trunks_port_del(removed_trunks,
                                                  netdev_sai_hw_id_get(port->up.netdev));
            ERRNO_LOG_EXIT(status, "Failed to remove reconfigure vlans");
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
                status = ops_sai_vlan_access_port_add(s->vlan,
                                                      netdev_sai_hw_id_get(port->up.netdev));
                ERRNO_LOG_EXIT(status, "Failed to reconfigure vlans");
            }
        }
        break;

    case PORT_VLAN_TRUNK:
        LIST_FOR_EACH_SAFE(port, next_port, bundle_node, &bundle->ports) {
            status = ops_sai_vlan_trunks_port_add(added_trunks,
                                                  netdev_sai_hw_id_get(port->up.netdev));
            ERRNO_LOG_EXIT(status, "Failed to reconfigure vlans");
        }
        break;

    case PORT_VLAN_NATIVE_UNTAGGED:
        LIST_FOR_EACH_SAFE(port, next_port, bundle_node, &bundle->ports) {
            if (tag_changed || mod_changed) {
                status = ops_sai_vlan_access_port_add(s->vlan,
                                                      netdev_sai_hw_id_get(port->up.netdev));
                ERRNO_LOG_EXIT(status, "Failed to reconfigure vlans");
            }
            status = ops_sai_vlan_trunks_port_add(added_trunks,
                                                  netdev_sai_hw_id_get(port->up.netdev));
            ERRNO_LOG_EXIT(status, "Failed to reconfigure vlans");
        }
        break;

    case PORT_VLAN_NATIVE_TAGGED:
        LIST_FOR_EACH_SAFE(port, next_port, bundle_node, &bundle->ports) {
            if (tag_changed || mod_changed) {
                status = __native_tagged_vlan_set(s->vlan,
                                                  netdev_sai_hw_id_get(port->up.netdev),
                                                  true);
                ERRNO_LOG_EXIT(status, "Failed to reconfigure vlans");
            }
            status = ops_sai_vlan_trunks_port_add(added_trunks,
                                                  netdev_sai_hw_id_get(port->up.netdev));
            ERRNO_LOG_EXIT(status, "Failed to reconfigure vlans");
        }
        break;

    default:
        ovs_assert(false);
    }

    bundle->vlan = s->vlan;
    bundle->vlan_mode = s->vlan_mode;
    __trunks_realloc(bundle, s->trunks);

exit:
    return status;
}

static int
__ofbundle_ports_reconfigure(struct ofbundle_sai *bundle,
                               const struct ofproto_bundle_settings *s)
{
    size_t i;
    bool port_found = false;
    int status = 0;
    struct ofport_sai *port = NULL, *next_port = NULL, *s_port = NULL;

    /* Figure out which ports were removed. */
    LIST_FOR_EACH_SAFE(port, next_port, bundle_node, &bundle->ports) {
        port_found = false;
        for (i = 0; i < s->n_slaves; i++) {
            s_port = __get_ofp_port(bundle->ofproto, s->slaves[i]);
            if (port == s_port) {
                port_found = true;
                break;
            }
        }
        if (!port_found) {
            status = __ofbundle_port_del(port);
            ERRNO_LOG_EXIT(status, "Failed to reconfigure ports");
        }
    }

    status = __vlan_reconfigure(bundle, s);
    ERRNO_LOG_EXIT(status, "Failed to reconfigure ports");

    /* Figure out which ports were added. */
    port = NULL;
    next_port = NULL;
    for (i = 0; i < s->n_slaves; i++) {
        port_found = false;
        s_port = __get_ofp_port(bundle->ofproto, s->slaves[i]);

        LIST_FOR_EACH_SAFE(port, next_port, bundle_node, &bundle->ports) {
            if (port == s_port) {
                port_found = true;
                break;
            }
        }

        if (!port_found) {
            status = __ofbundle_port_add(bundle, s_port);
        }
        ERRNO_LOG_EXIT(status, "Failed to reconfigure ports");
    }

exit:
    return status;
}

/*
 * Rename bundle or delete name if new name is NULL.
 */
static void
__ofbundle_rename(struct ofbundle_sai *bundle, const char *name)
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

/*
 * Create new bundle and perform basic initialization.
 */
static struct ofbundle_sai *
__ofbundle_create(struct ofproto_sai *ofproto, void *aux,
                    const struct ofproto_bundle_settings *s)
{
    struct ofbundle_sai *bundle = xzalloc(sizeof (struct ofbundle_sai));

    hmap_insert(&ofproto->bundles, &bundle->hmap_node, hash_pointer(aux, 0));
    list_init(&bundle->ports);
    __ofbundle_rename(bundle, s->name);
    __trunks_realloc(bundle, s->trunks);

    bundle->ofproto = ofproto;
    bundle->aux = aux;

    return bundle;
}

/*
 * Destroy bundle and remove ports from it.
 */
static void
__ofbundle_destroy(struct ofbundle_sai *bundle)
{
    int status = 0;
    struct ofport_sai *port = NULL, *next_port = NULL;

    if (NULL == bundle) {
        return;
    }

    LIST_FOR_EACH_SAFE(port, next_port, bundle_node, &bundle->ports) {
        status = __ofbundle_port_del(port);
        ERRNO_LOG_EXIT(status, "Failed to destroy bundle");
    }

exit:
    __ofbundle_rename(bundle, NULL);
    __trunks_realloc(bundle, NULL);
    hmap_remove(&bundle->ofproto->bundles, &bundle->hmap_node);

    free(bundle);
}

/*
 * Find bundle by aux.
 */
static struct ofbundle_sai *
__ofbundle_lookup(struct ofproto_sai *ofproto, void *aux)
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

/*
 * Apply bundle settings.
 */
static int
__bundle_set(struct ofproto *ofproto_, void *aux,
                       const struct ofproto_bundle_settings *s)
{
    struct ofbundle_sai *bundle = NULL;
    int status = 0;
    struct ofproto_sai *ofproto = __ofproto_sai_cast(ofproto_);

    SAI_API_TRACE_FN();

    if ((s && !strcmp(s->name, DEFAULT_BRIDGE_NAME)) ||
        strcmp(ofproto_->type, SAI_INTERFACE_TYPE_SYSTEM)) {
        goto exit;
    }

    bundle = __ofbundle_lookup(ofproto, aux);
    if (NULL == s) {
        __ofbundle_destroy(bundle);
        goto exit;
    }

    if (NULL == bundle) {
        bundle = __ofbundle_create(ofproto, aux, s);
    }

    status = __ofbundle_ports_reconfigure(bundle, s);
    ERRNO_LOG_EXIT(status, "Failed to set bundle");

exit:
    return status;
}

static void
__bundle_remove(struct ofport *port_)
{
    int status = 0;
    struct ofport_sai *port = __ofport_sai_cast(port_);
    struct ofbundle_sai *bundle = port->bundle;

    SAI_API_TRACE_FN();

    if (NULL == bundle) {
        return;
    }

    status = __ofbundle_port_del(port);
    ERRNO_LOG_EXIT(status, "Failed to remove bundle");

exit:
    if (list_is_empty(&bundle->ports)) {
        __ofbundle_destroy(bundle);
    }
}

static int
__bundle_get(struct ofproto *ofproto_, void *aux, int *bundle_handle)
{
    SAI_API_TRACE_FN();

    return 0;
}

static int
__set_vlan(struct ofproto *ofproto, int vid, bool add)
{
    SAI_API_TRACE_FN();

    return ops_sai_vlan_set(vid, add);
}

static inline struct ofproto_sai_group *
__ofproto_sai_group_cast(const struct ofgroup *group)
{
    return group ? CONTAINER_OF(group, struct ofproto_sai_group, up) : NULL;
}

static struct ofgroup *
__group_alloc(void)
{
    struct ofproto_sai_group *group = xzalloc(sizeof *group);

    SAI_API_TRACE_NOT_IMPLEMENTED_FN();

    return &group->up;
}

static enum ofperr
__group_construct(struct ofgroup *group_)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();

    return 0;
}

static void
__group_destruct(struct ofgroup *group_)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();

    return;
}

static void
__group_dealloc(struct ofgroup *group_)
{
    struct ofproto_sai_group *group = __ofproto_sai_group_cast(group_);

    SAI_API_TRACE_NOT_IMPLEMENTED_FN();

    free(group);
}

static enum ofperr
__group_modify(struct ofgroup *group_)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();

    return 0;
}

static enum ofperr
__group_get_stats(const struct ofgroup *group_,
                  struct ofputil_group_stats *ogs)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();

    return 0;
}

static const char *
__get_datapath_version(const struct ofproto *ofproto_ OVS_UNUSED)
{
    SAI_API_TRACE_FN();

    return strdup(SAI_DATAPATH_VERSION);
}

static int
__add_l3_host_entry(const struct ofproto *ofproto_, void *aux,
                              bool is_ipv6_addr, char *ip_addr,
                              char *next_hop_mac_addr, int *l3_egress_id)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();

    return 0;
}

static int
__delete_l3_host_entry(const struct ofproto *ofproto_, void *aux,
                                 bool is_ipv6_addr, char *ip_addr,
                                 int *l3_egress_id)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();

    return 0;
}

static int
__get_l3_host_hit_bit(const struct ofproto *ofproto_, void *aux,
                                bool is_ipv6_addr, char *ip_addr,
                                bool *hit_bit)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();

    return 0;
}

static int
__l3_route_action(const struct ofproto *ofprotop,
                            enum ofproto_route_action action,
                            struct ofproto_route *routep)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();

    return 0;
}

static int
__l3_ecmp_set(const struct ofproto *ofprotop, bool enable)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();

    return 0;
}

static int
__l3_ecmp_hash_set(const struct ofproto *ofprotop, unsigned int hash,
                             bool enable)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();

    return 0;
}

static int
__run(struct ofproto *ofproto)
{
    SAI_API_TRACE_FN();

    return 0;
}

static void
__wait(struct ofproto *ofproto)
{
    SAI_API_TRACE_FN();
}

static void
__set_tables_version(struct ofproto *ofproto, cls_version_t version)
{
    SAI_API_TRACE_FN();

    return;
}
