/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#include <uuid.h>
#include <hmap.h>
#include <ofproto/ofproto-provider.h>
#include <sai-log.h>
#include <sai-common.h>
#include <sai-hash.h>
#include <sai-api-class.h>
#include <sai-ofproto-provider.h>
#include <ops-cls-asic-plugin.h>
#include "plugin-extensions.h"
#include "sai-classifier.h"

VLOG_DEFINE_THIS_MODULE(sai_classifier);

static struct sai_classifier_global_db classifier_global_db;

struct sai_classifier_rules {
    struct rule *of_rule;
    bool count;
};

struct sai_classifier_table {
    struct hmap_node node;
    struct uuid id; /* Table ID */
    char *name; /* Table name */
    struct ofproto_sai *ofproto; /* ACL container bridge */
    struct ofproto_sai *parent; /* Parent bridge */
    struct sai_classifier_rules rules[ACL_RULES_PER_TABLE_MAX]; /* rules */
    int active_rules; /* number of active rules */
    int bound_interfaces; /* Bound interfaces count */
    int table_index; /* Table index within internal DB */
};

static int __apply(struct ops_cls_list *, struct ofproto *,
                   void *, struct ops_cls_interface_info *,
                   enum ops_cls_direction, struct ops_cls_pd_status *);

static int __remove(const struct uuid *, const char *,
                    enum ops_cls_type, struct ofproto *,
                    void *, struct ops_cls_interface_info *,
                    enum ops_cls_direction, struct ops_cls_pd_status *);

static int __replace(const struct uuid *, const char *,
                     struct ops_cls_list *, struct ofproto *,
                     void *, struct ops_cls_interface_info *interface_nfo,
                     enum ops_cls_direction, struct ops_cls_pd_status *);

static int __list_update(struct ops_cls_list *, struct ops_cls_pd_list_status *);

static int __statistics_get(const struct uuid *, const char *,
                            enum ops_cls_type, struct ofproto *,
                            void *, struct ops_cls_interface_info *,
                            enum ops_cls_direction, struct ops_cls_statistics *,
                            int, struct ops_cls_pd_list_status *);

static int __statistics_clear(const struct uuid *, const char *,
                              enum ops_cls_type, struct ofproto *,
                              void *, struct ops_cls_interface_info *,
                              enum ops_cls_direction,
                              struct ops_cls_pd_list_status *);

static int __statistics_clear_all(struct ops_cls_pd_list_status *);
static int __log_pkt_register_cb(void (*callback_handler)(struct acl_log_info *));

/***********************************************
 * ops-cls-asic-plugin API
 *
 * Implementation for required ASIC plugin API
 *
 **********************************************/
const struct ops_cls_plugin_interface cls_sai_class = {
    PROVIDER_INIT_GENERIC(ofproto_ops_cls_apply,                    __apply)
    PROVIDER_INIT_GENERIC(ofproto_ops_cls_remove,                   __remove)
    PROVIDER_INIT_GENERIC(ofproto_ops_cls_replace,                  __replace)
    PROVIDER_INIT_GENERIC(ofproto_ops_cls_list_update,              __list_update)
    PROVIDER_INIT_GENERIC(ofproto_ops_cls_statistics_get,           __statistics_get)
    PROVIDER_INIT_GENERIC(ofproto_ops_cls_statistics_clear,           __statistics_clear)
    PROVIDER_INIT_GENERIC(ofproto_ops_cls_statistics_clear_all,       __statistics_clear_all)
    PROVIDER_INIT_GENERIC(ofproto_ops_cls_acl_log_pkt_register_cb,  __log_pkt_register_cb)
};

static struct plugin_extension_interface cls_sai_extension = {
    OPS_CLS_ASIC_PLUGIN_INTERFACE_NAME,
    OPS_CLS_ASIC_PLUGIN_INTERFACE_MAJOR,
    OPS_CLS_ASIC_PLUGIN_INTERFACE_MINOR,
    (void *)&cls_sai_class
};

/* hash map to store ACL Tables*/
static struct hmap tables_map;

/* A list of all fields in a rule, later used to generate function headers
 * and number of fields
 */
#define OPS_MATCH_FIELDS                                     \
    MATCH_FIELD(SRC_IPV4,   MFF_IPV4_SRC,       ipv4_src)    \
    MATCH_FIELD(DST_IPV4,   MFF_IPV4_DST,       ipv4_dst)    \
    MATCH_FIELD(SRC_IPV6,   MFF_IPV6_SRC,       ipv6_src)    \
    MATCH_FIELD(DST_IPV6,   MFF_IPV6_DST,       ipv6_dst)    \
    MATCH_FIELD(PROTOCOL,   MFF_IP_PROTO,       protocol)    \
    MATCH_FIELD(TCP_FLAGS,  MFF_TCP_FLAGS,      tcp_flags)   \
    MATCH_FIELD(SRC_L4_PORT,MFF_TCP_SRC,        src_l4_port) \
    MATCH_FIELD(DST_L4_PORT,MFF_TCP_DST,        dst_l4_port) \
    MATCH_FIELD(ICMP_CODE,  MFF_ICMPV4_CODE,    icmp_code)   \
    MATCH_FIELD(ICMP_TYPE,  MFF_ICMPV4_TYPE,    icmp_type)   \
    MATCH_FIELD(DSCP,       MFF_IP_DSCP,        dscp)        \
    MATCH_FIELD(SRC_MAC,    MFF_ETH_SRC,        src_mac)     \
    MATCH_FIELD(DST_MAC,    MFF_ETH_DST,        dst_mac)     \
    MATCH_FIELD(VLAN,       MFF_DL_VLAN,        vlan)        \
    MATCH_FIELD(L2_COS,     MFF_DL_VLAN_PCP,    l2_cos)      \
    MATCH_FIELD(ETHERTYPE,  MFF_ETH_TYPE,       ethertype)

/* Field indexes. */
enum {
#define MATCH_FIELD(ENUM, FLAG, FIELD) OPS_CLS_FIELD_##ENUM,
    OPS_MATCH_FIELDS
#undef MATCH_FIELD
    N_MATCH_FIELDS
};

/* Declare convert functions */
#define MATCH_FIELD(ENUM, FLAG, FIELD)                                      \
    static void                                                             \
    ops_cls_fields_match_##ENUM(                                            \
            const struct ops_cls_list_entry_match_fields *ops_match,        \
            struct match *match);

OPS_MATCH_FIELDS
#undef MATCH_FIELD

/* Array of convert functions */
static void
(*ops_cls_field_to_match_field_func[N_MATCH_FIELDS])(const struct ops_cls_list_entry_match_fields *ops_match,    \
                                                     struct match *match) = {
#define MATCH_FIELD(ENUM, FLAG, FIELD) ops_cls_fields_match_##ENUM,
     OPS_MATCH_FIELDS
#undef MATCH_FIELD
};

static void
ops_cls_fields_match_SRC_IPV4(const struct ops_cls_list_entry_match_fields *ops_match,
                              struct match *match)
{
    if ((ops_match->entry_flags & OPS_CLS_SRC_IPADDR_VALID) == 0) {
        return;
    }

    if (ops_match->src_addr_family != OPS_CLS_AF_INET) {
        return;
    }

    match->flow.metadata |= MFF_IPV4_SRC;
    match->wc.masks.metadata |= MFF_IPV4_SRC;
    match_set_nw_src_masked(match,
                            htonl(ops_match->src_ip_address.v4.s_addr),
                            htonl(ops_match->src_ip_address_mask.v4.s_addr));
}

static void
ops_cls_fields_match_DST_IPV4(const struct ops_cls_list_entry_match_fields *ops_match,
                              struct match *match)
{
    if ((ops_match->entry_flags & OPS_CLS_DEST_IPADDR_VALID) == 0) {
        return;
    }

    if (ops_match->src_addr_family != OPS_CLS_AF_INET) {
        return;
    }

    match->flow.metadata |= MFF_IPV4_DST;
    match->wc.masks.metadata |= MFF_IPV4_DST;
    match_set_nw_dst_masked(match,
                            htonl(ops_match->dst_ip_address.v4.s_addr),
                            htonl(ops_match->dst_ip_address_mask.v4.s_addr));
}

static void
ops_cls_fields_match_SRC_IPV6(const struct ops_cls_list_entry_match_fields *ops_match,
                              struct match *match)
{
    if ((ops_match->entry_flags & OPS_CLS_SRC_IPADDR_VALID) == 0) {
        return;
    }

    if (ops_match->src_addr_family != OPS_CLS_AF_INET6) {
        return;
    }

    match->flow.metadata |= MFF_IPV6_SRC;
    match->wc.masks.metadata |= MFF_IPV6_SRC;
    match_set_ipv6_src_masked(match,
                              &ops_match->src_ip_address.v6,
                              &ops_match->src_ip_address_mask.v6);
}

static void
ops_cls_fields_match_DST_IPV6(const struct ops_cls_list_entry_match_fields *ops_match,
                              struct match *match)
{
    if ((ops_match->entry_flags & OPS_CLS_DEST_IPADDR_VALID) == 0) {
        return;
    }

    if (ops_match->src_addr_family != OPS_CLS_AF_INET6) {
        return;
    }

    match->flow.metadata |= MFF_IPV6_DST;
    match->wc.masks.metadata |= MFF_IPV6_DST;
    match_set_ipv6_dst_masked(match,
                              &ops_match->dst_ip_address.v6,
                              &ops_match->dst_ip_address_mask.v6);
}

static void
ops_cls_fields_match_TCP_FLAGS(const struct ops_cls_list_entry_match_fields *ops_match,
                              struct match *match)
{
    if ((ops_match->entry_flags & OPS_CLS_TCP_FLAGS_VALID) == 0) {
        return;
    }

    match->flow.metadata |= MFF_TCP_FLAGS;
    match->wc.masks.metadata |= MFF_TCP_FLAGS;
    match_set_tcp_flags_masked(match,
                               htons(ops_match->tcp_flags),
                               htons(ops_match->tcp_flags_mask));
}

static void
ops_cls_fields_match_ICMP_CODE(const struct ops_cls_list_entry_match_fields *ops_match,
                              struct match *match)
{
    if ((ops_match->entry_flags & OPS_CLS_ICMP_CODE_VALID) == 0) {
        return;
    }
    match->flow.metadata |= MFF_ICMPV4_CODE;
    match->wc.masks.metadata |= MFF_ICMPV4_CODE;
    match_set_icmp_code(match, ops_match->icmp_code);
}

static void
ops_cls_fields_match_SRC_L4_PORT(const struct ops_cls_list_entry_match_fields *ops_match,
                                 struct match *match)
{
    uint16_t mask = 0xFFFF;
    uint16_t port = 0;
    if ((ops_match->entry_flags & OPS_CLS_L4_SRC_PORT_VALID) == 0) {
        return;
    }
    /* Support only equal for now */
    if (ops_match->L4_src_port_op != OPS_CLS_L4_PORT_OP_EQ &&
        ops_match->L4_src_port_op != OPS_CLS_L4_PORT_OP_NEQ) {
        return;
    }

    match->flow.metadata |= MFF_TCP_SRC;
    match->wc.masks.metadata |= MFF_TCP_SRC;
    port = ops_match->L4_src_port_min;
    if (ops_match->L4_src_port_op == OPS_CLS_L4_PORT_OP_NEQ) {
        mask = ~port;
        port = 0;
    }
    match_set_tp_src_masked(match, htons(port), htons(mask));
}

static void
ops_cls_fields_match_DST_L4_PORT(const struct ops_cls_list_entry_match_fields *ops_match,
                                 struct match *match)
{
    uint16_t mask = 0xFFFF;
    uint16_t port = 0;
    if ((ops_match->entry_flags & OPS_CLS_L4_DEST_PORT_VALID) == 0) {
        return;
    }
    /* Support only equal for now */
    if (ops_match->L4_dst_port_op != OPS_CLS_L4_PORT_OP_EQ &&
        ops_match->L4_dst_port_op != OPS_CLS_L4_PORT_OP_NEQ) {
        return;
    }

    match->flow.metadata |= MFF_TCP_DST;
    match->wc.masks.metadata |= MFF_TCP_DST;
    port = ops_match->L4_dst_port_min;
    if (ops_match->L4_dst_port_op == OPS_CLS_L4_PORT_OP_NEQ) {
        mask = ~port;
        port = 0;
    }
    match_set_tp_dst_masked(match, htons(port), htons(mask));
}

static void
ops_cls_fields_match_ICMP_TYPE(const struct ops_cls_list_entry_match_fields *ops_match,
                               struct match *match)
{
    if ((ops_match->entry_flags & OPS_CLS_ICMP_TYPE_VALID) == 0) {
        return;
    }
    match->flow.metadata |= MFF_ICMPV4_TYPE;
    match->wc.masks.metadata |= MFF_ICMPV4_TYPE;
    match_set_icmp_type(match, ops_match->icmp_type);
}

static void
ops_cls_fields_match_DSCP(const struct ops_cls_list_entry_match_fields *ops_match,
                          struct match *match)
{
#define DSCP_MASK 0xfc
    if ((ops_match->entry_flags & OPS_CLS_DSCP_VALID) == 0) {
        return;
    }
    match->flow.metadata |= MFF_IP_DSCP;
    match->wc.masks.metadata |= MFF_IP_DSCP;

    match->wc.masks.nw_tos |= ops_match->tos_mask;
    match->flow.nw_tos &= ~DSCP_MASK;
    match->flow.nw_tos |= ops_match->tos;
}

static void
ops_cls_fields_match_PROTOCOL(const struct ops_cls_list_entry_match_fields *ops_match,
                              struct match *match)
{
    if ((ops_match->entry_flags & OPS_CLS_PROTOCOL_VALID) == 0) {
        return;
    }

    match->flow.metadata |= MFF_IP_PROTO;
    match_set_nw_proto(match, ops_match->protocol);
}

static void
ops_cls_fields_match_VLAN(const struct ops_cls_list_entry_match_fields *ops_match,
                          struct match *match)
{
    if ((ops_match->entry_flags & OPS_CLS_VLAN_VALID) == 0) {
        return;
    }

    match->flow.metadata |= MFF_DL_VLAN;
    match_set_dl_vlan(match, htons(ops_match->vlan));
}

static void
ops_cls_fields_match_L2_COS(const struct ops_cls_list_entry_match_fields *ops_match,
                            struct match *match)
{
    if ((ops_match->entry_flags & OPS_CLS_L2_COS_VALID) == 0) {
        return;
    }

    match->flow.metadata |= MFF_DL_VLAN_PCP;
    match_set_dl_vlan_pcp(match, ops_match->L2_cos);
}

static void
ops_cls_fields_match_SRC_MAC(const struct ops_cls_list_entry_match_fields *ops_match,
                             struct match *match)
{
    struct eth_addr mac,mask;
    if ((ops_match->entry_flags & OPS_CLS_SRC_MAC_VALID) == 0) {
        return;
    }

    match->flow.metadata |= MFF_ETH_SRC;
    match->wc.masks.metadata |= MFF_ETH_SRC;
    memcpy(mac.ea, ops_match->src_mac, sizeof(mac.ea));
    memcpy(mask.ea, ops_match->src_mac_mask, sizeof(mask.ea));
    match_set_dl_src_masked(match, mac, mask);
}

static void
ops_cls_fields_match_DST_MAC(const struct ops_cls_list_entry_match_fields *ops_match,
                             struct match *match)
{
    struct eth_addr mac,mask;
    if ((ops_match->entry_flags & OPS_CLS_DST_MAC_VALID) == 0) {
        return;
    }

    match->flow.metadata |= MFF_ETH_DST;
    match->wc.masks.metadata |= MFF_ETH_DST;
    memcpy(mac.ea, ops_match->dst_mac, sizeof(mac.ea));
    memcpy(mask.ea, ops_match->dst_mac_mask, sizeof(mask.ea));
    match_set_dl_dst_masked(match, mac, mask);
}

static void
ops_cls_fields_match_ETHERTYPE(const struct ops_cls_list_entry_match_fields *ops_match,
                               struct match *match)
{
    if ((ops_match->entry_flags & OPS_CLS_L2_ETHERTYPE_VALID) == 0) {
        return;
    }
    match->flow.metadata |= MFF_ETH_TYPE;
    match_set_dl_type(match, htons(ops_match->L2_ethertype));
}

/*
 * Assign a local index to a created table
 * This is used later for resource accounting
 */
static int
__table_assign_index(struct sai_classifier_table *table)
{
    int i = 0;
    int status = 0;

    ovs_assert(table != NULL);

    while (i < ACL_TABLES_MAX) {
        if (classifier_global_db.existing_tables[i] == NULL) {
            classifier_global_db.existing_tables[i] = table;
            table->table_index = i;
        }
        i++;
    }

    if (i == ACL_TABLES_MAX) {
        status = OPS_CLS_STATUS_HW_INTERNAL_ERR;
        ERRNO_LOG_EXIT(status, "Reached maximum tables allocation (%d)",
                       ACL_TABLES_MAX);
    }

    exit:
    return 0;
}

/*
 * Free table index
 */
static void
__table_release_index(struct sai_classifier_table *table)
{
    ovs_assert(table != NULL);
    classifier_global_db.existing_tables[table->table_index] = NULL;
    table->table_index = 0;
}

/*
 * Table lookup in hash table
 */
static struct sai_classifier_table*
__tables_db_lookup(const struct uuid *table_id)
{
    struct sai_classifier_table *table = NULL;
    size_t id;

    ovs_assert(table_id != NULL);
    id = uuid_hash(table_id);

    HMAP_FOR_EACH_WITH_HASH(table, node, id, &tables_map) {
        if (uuid_equals(&table->id, table_id)) {
            return table;
        }
    }
    return NULL;
}

/*
 * Add ACL table to hash (key uuid)
 */
static struct sai_classifier_table*
__tables_db_add(struct sai_classifier_table* table)
{
    ovs_assert(table != NULL);
    hmap_insert(&tables_map, &table->node, uuid_hash(&(table->id)));
    return table;
}

/*
 * Allocate classifier table struct
 * and init the values
 */
static struct sai_classifier_table*
__table_construct(struct ops_cls_list  *clist, struct ofproto *ofproto)
{
    struct sai_classifier_table *table = NULL;

    ovs_assert(clist != NULL);

    table = xzalloc(sizeof(*table));

    table->id = clist->list_id;
    table->name = xstrdup(clist->list_name);
    table->parent = ofproto_sai_cast(ofproto);
    table->bound_interfaces = 0;
    table->active_rules = 0;
    __table_assign_index(table);

    return table;
}

/*
 * Delete ACL table from hash table
 */
static void
__tables_db_del(struct sai_classifier_table *table)
{
    ovs_assert(table != NULL);

    hmap_remove(&tables_map, &table->node);
    VLOG_DBG("Removed ACL %s in hashmap", table->name);
}

/*
 * Table destruction (DB)
 */
static void
__table_destruct(struct sai_classifier_table *table)
{
    int i = 0;

    ovs_assert(table != NULL);

    /* Clear rules */
    for (i = 0;i < table->active_rules;i++ ) {
        ofproto_sai_class.rule_dealloc(table->rules[i].of_rule);
        table->rules[i].of_rule = NULL;
        table->rules[i].count = 0;
    }

    __table_release_index(table);
    free(table->name);
    free(table);
}

/*
 * Convert ACL Entry to OF Rule
 */
static void
__acl_entry_to_classifier_rule(int rule_id,
                               const struct ops_cls_list_entry *entry,
                               struct sai_classifier_rules *sai_rule)
{
    uint8_t i = 0;
    struct match ofmatch;
    struct ofpact ofaction;

    ovs_assert(entry != NULL);
    ovs_assert(sai_rule != NULL);

    if (entry->entry_actions.action_flags == 0) {
        /* Rule with no action - ignore */
        return;
    }

    match_init_catchall(&ofmatch);
    /* Convert Fields to match struct */
    for (i = 0; i < N_MATCH_FIELDS; i++) {
        ops_cls_field_to_match_field_func[i](&entry->entry_fields, &ofmatch);
    }

    /* Update match in rule */
    cls_rule_init(CONST_CAST(struct cls_rule *, &sai_rule->of_rule->cr),
                             &ofmatch, ACL_RULES_PER_TABLE_MAX - rule_id);
    /* TODO : this cookie should represent a unique flow ID to see if it has
     * changed or a new one is created */
    sai_rule->of_rule->flow_cookie = rule_id;

    /* rule priority */
    *CONST_CAST(uint16_t *, &sai_rule->of_rule->importance) = ACL_RULES_PER_TABLE_MAX - rule_id;

    /* Convert Actions */
    sai_rule->count = false;
    if (entry->entry_actions.action_flags & OPS_CLS_ACTION_DENY) {
        *CONST_CAST(const struct rule_actions **, &sai_rule->of_rule->actions)
                = rule_actions_create(NULL, 0);
    }
    if (entry->entry_actions.action_flags & OPS_CLS_ACTION_PERMIT) {
        ofaction.type = OFPACT_CLEAR_ACTIONS;
        *CONST_CAST(const struct rule_actions **, &sai_rule->of_rule->actions)
                        = rule_actions_create(&ofaction, 1);
    }
    if (entry->entry_actions.action_flags & OPS_CLS_ACTION_LOG) {
        /* Future support */
        sai_rule->count = true;
        sai_rule->of_rule->flags &= ~OFPUTIL_FF_NO_PKT_COUNTS;
    }
    if (entry->entry_actions.action_flags & OPS_CLS_ACTION_COUNT) {
        /* Future support */
        sai_rule->count = true;
        sai_rule->of_rule->flags &= ~OFPUTIL_FF_NO_PKT_COUNTS;
    }
}

/*
 * Check that there are enough resources per ACL
 * The check is made per rule, but within a context of a list
 * So planned_usage will increase per rule, and we are always
 * checking if there is room for one rule considering the current global usage
 * and assuming the planned_usage is taken
 *
 * At the end of the resource check global_usage would be essentially incremented by planned_usage.
 *
 * @return 0 if no issue with resources
 */
static int
__check_rule_resources_available(const struct ops_cls_list_entry *entry,
                                 enum ops_cls_direction direction,
                                 struct sai_classifier_resources *planned_usage)
{
    ovs_assert(entry != NULL);
    ovs_assert(planned_usage != NULL);

    /* Safety since we only support ingress for now */
    if (direction != OPS_CLS_DIRECTION_IN) {
        return 1;
    }

    /* Number of rules */
    planned_usage->rules++;
    if (planned_usage->rules > ACL_RULES_PER_TABLE_MAX) {
        return 1;
    }

    /* Number of counters */
    if ((entry->entry_actions.action_flags & OPS_CLS_ACTION_LOG) ||
        (entry->entry_actions.action_flags & OPS_CLS_ACTION_COUNT)) {
        planned_usage->counters++;
        if (classifier_global_db.global_use.counters + planned_usage->counters > ACL_COUNTERS_MAX) {
            return 1;
        }
    }

    /* L4 port range resource */
    if ((entry->entry_fields.L4_dst_port_op != OPS_CLS_L4_PORT_OP_NONE) &&
        (entry->entry_fields.L4_dst_port_op != OPS_CLS_L4_PORT_OP_EQ) &&
        (entry->entry_fields.L4_dst_port_op != OPS_CLS_L4_PORT_OP_NEQ)) {
        planned_usage->acl_range++;
        /* Check port range availability */
        /* TODO : Reuse port range entries ? */
        if (classifier_global_db.global_use.acl_range + planned_usage->acl_range > ACL_L4_RANGE_MAX) {
            return 1;
        }
    }

    if ((entry->entry_fields.L4_src_port_op != OPS_CLS_L4_PORT_OP_NONE) &&
        (entry->entry_fields.L4_src_port_op != OPS_CLS_L4_PORT_OP_EQ) &&
        (entry->entry_fields.L4_src_port_op != OPS_CLS_L4_PORT_OP_NEQ)) {
        planned_usage->acl_range++;
        /* Check port range availability */
        /* TODO : Reuse port range entries ? */
        if (classifier_global_db.global_use.acl_range + planned_usage->acl_range > ACL_L4_RANGE_MAX) {
            return 1;
        }
    }
    return 0;
}

static void
__clear_table_resources(int table_index)
{
    classifier_global_db.global_use.acl_range -= classifier_global_db.per_table_use[table_index].acl_range;
    classifier_global_db.global_use.counters -= classifier_global_db.per_table_use[table_index].counters;
    classifier_global_db.global_use.rules -= classifier_global_db.per_table_use[table_index].rules;

    classifier_global_db.per_table_use[table_index].acl_range = 0;
    classifier_global_db.per_table_use[table_index].counters = 0;
    classifier_global_db.per_table_use[table_index].rules = 0;
}

static int
__table_destroy_if_needed(struct sai_classifier_table *table)
{
    int status = 0;

    ovs_assert(table != NULL);

    if (table->bound_interfaces == 0) {
        /* Account for resources */
        __clear_table_resources(table->table_index);
        ofproto_sai_class.destruct(&table->ofproto->up);
        ofproto_sai_class.dealloc(&table->ofproto->up);

        __tables_db_del(table);
        __table_destruct(table);
    }
    return status;
}

static void
__update_resource_usage(int table_index,
                        const struct sai_classifier_resources *additional_usage)
{
    ovs_assert(additional_usage != NULL);

    classifier_global_db.global_use.acl_range += additional_usage->acl_range;
    classifier_global_db.global_use.counters += additional_usage->counters;
    classifier_global_db.global_use.rules += additional_usage->rules;

    classifier_global_db.per_table_use[table_index].acl_range += additional_usage->acl_range;
    classifier_global_db.per_table_use[table_index].counters += additional_usage->counters;
    classifier_global_db.per_table_use[table_index].rules += additional_usage->rules;
}

static void
__rule_base_init(struct rule *ofrule, struct ofproto *ofproto)
{
    ovs_assert(ofrule != NULL);
    ovs_assert(ofproto != NULL);

    *CONST_CAST(struct ofproto **, &ofrule->ofproto) = ofproto;
    ovs_refcount_init(&ofrule->ref_count);
    ofrule->created = ofrule->modified = time_msec();

    ovs_mutex_init(&ofrule->mutex);
    ovs_mutex_lock(&ofrule->mutex);
    ofrule->idle_timeout = 0;
    ofrule->hard_timeout = 0;

    ofrule->removed_reason = OVS_OFPRR_NONE;

    *CONST_CAST(uint8_t *, &ofrule->table_id) = 1;
    ofrule->flags = OFPUTIL_FF_NO_PKT_COUNTS | OFPUTIL_FF_NO_BYT_COUNTS;
    list_init(&ofrule->meter_list_node);
    ofrule->eviction_group = NULL;
    list_init(&ofrule->expirable);
    ofrule->monitor_flags = 0;
    ofrule->add_seqno = 0;
    ofrule->modify_seqno = 0;
}

static int
__write_table(struct ops_cls_list *list,
              struct ofproto *ofproto_,
              enum ops_cls_direction direction,
              struct sai_classifier_table** table)
{
    int status = 0;
    int rule_id = 0;
    struct rule *ofrule = NULL;
    struct ofproto *acl_ofproto = NULL;
    bool acl_ofproto_constructed = false;

    ovs_assert(list != NULL);
    ovs_assert(ofproto_ != NULL);
    ovs_assert(table != NULL);

    *table = __table_construct(list, ofproto_);
    if (*table == NULL) {
        status = OPS_CLS_STATUS_HW_INTERNAL_ERR;
        ERRNO_LOG_EXIT(status, "Failed to allocate memory for Table %s",
                       list->list_name);
    }
    /* Create table using OF API */
    acl_ofproto = ofproto_sai_class.alloc();
    if (!acl_ofproto) {
        status = OPS_CLS_STATUS_HW_INTERNAL_ERR;
        ERRNO_LOG_EXIT(status, "Failed to construct Table %s", list->list_name);
    }
    acl_ofproto->ofproto_class = &ofproto_sai_class;
    (*table)->ofproto = ofproto_sai_cast(acl_ofproto);
    acl_ofproto->type = xstrdup(direction == OPS_CLS_DIRECTION_IN ? SAI_TYPE_IACL:SAI_TYPE_EACL);
    acl_ofproto->name = xstrdup(list->list_name);
    status = ofproto_sai_class.construct(acl_ofproto);
    ERRNO_LOG_EXIT(status, "Failed to construct Table %s", list->list_name);
    acl_ofproto_constructed = true;

    /* Insert all rules to the table */
    for (rule_id = 0;rule_id < list->num_entries;rule_id++) {
        ofrule = ofproto_sai_class.rule_alloc();
        if (!ofrule) {
            status = OPS_CLS_STATUS_HW_INTERNAL_ERR;
            ERRNO_EXIT(status);
        }
        __rule_base_init(ofrule, acl_ofproto);
        (*table)->rules[rule_id].of_rule = ofrule;
        /* Convert rule fields */
        __acl_entry_to_classifier_rule(rule_id,
                                       &(list->entries[rule_id]),
                                       &((*table)->rules[rule_id]));
        status = ofproto_sai_class.rule_construct(ofrule);
        ERRNO_LOG_EXIT(status, "Failed to construct rule index %d in Table %s",
                       rule_id, list->list_name);
        ofproto_sai_class.rule_insert(ofrule, NULL, 0);
    }

    exit:
    /* Rollback */
    if (status) {
        if (acl_ofproto) {
            if (acl_ofproto_constructed) {
                ofproto_sai_class.destruct(acl_ofproto);
            }
            ofproto_sai_class.dealloc(acl_ofproto);
        }
        if (*table) {
            __table_destruct(*table);
        }
    }
    return status;
}

static int
__apply(struct ops_cls_list *list, struct ofproto *ofproto_,
        void *aux, struct ops_cls_interface_info *interface_info,
        enum ops_cls_direction direction, struct ops_cls_pd_status *pd_status)
{
    int status = OPS_CLS_STATUS_SUCCESS;
    int rule_id = 0;
    struct sai_classifier_table* table = NULL;
    struct sai_classifier_resources current_use;
    struct ofproto_bundle_settings bundle_settings;

    SAI_API_TRACE_FN();

    ovs_assert(list != NULL);
    ovs_assert(ofproto_ != NULL);
    ovs_assert(interface_info != NULL);
    ovs_assert(pd_status != NULL);

    memset(&current_use, 0 , sizeof(current_use));
    memset(&bundle_settings, 0 , sizeof(bundle_settings));

    /* Search if table is already in HW */
    table = __tables_db_lookup(&list->list_id);
    if (table == NULL) {

        /* Check resources are available */
        for (rule_id = 0;rule_id < list->num_entries;rule_id++) {
            status = __check_rule_resources_available(&(list->entries[rule_id]),
                                                      direction,
                                                      &current_use);
            if (status) {
                pd_status->entry_id = rule_id;
                pd_status->status_code = OPS_CLS_STATUS_HW_RESOURCE_ERR;
                VLOG_INFO("Insufficient resources for ACL apply %s ",
                          list->list_name);
                ERRNO_EXIT(status);
            }
        }

        /* Convert rules, Save table in DB */
        status = __write_table(list, ofproto_, direction, &table);
        ERRNO_EXIT(status);
        /* Update resources */
        __update_resource_usage(table->table_index, &current_use);
        table = __tables_db_add(table);
    }

    /* Bind Table to port */
    if (interface_info &&
        (interface_info->interface == OPS_CLS_INTERFACE_PORT) &&
        ((interface_info->flags & OPS_CLS_INTERFACE_L3ONLY) == 0))
    {
        status = ofproto_sai_class.bundle_set(&(table->ofproto->up),
                                              aux, &bundle_settings);
        ERRNO_LOG_EXIT(status, "Failed to Apply table %s to port",
                       list->list_name);
        table->bound_interfaces++;
    }

    exit:
    SAI_API_TRACE_EXIT_FN();
    return status;
}

static int
__remove(const struct uuid *list_id, const char *list_name,
         enum ops_cls_type list_type, struct ofproto *ofproto_,
         void *aux, struct ops_cls_interface_info *interface_info,
         enum ops_cls_direction direction, struct ops_cls_pd_status *pd_status)
{
    int status = OPS_CLS_STATUS_SUCCESS;
    struct sai_classifier_table* table = NULL;

    SAI_API_TRACE_FN();

    ovs_assert(list_id != NULL);
    ovs_assert(ofproto_ != NULL);
    ovs_assert(list_name != NULL);
    ovs_assert(interface_info != NULL);
    ovs_assert(pd_status != NULL);

    /* table should be configured in HW */
    table = __tables_db_lookup(list_id);
    if (table == NULL) {
        status = OPS_CLS_STATUS_HW_INTERNAL_ERR;
        ERRNO_LOG_EXIT(status,
                       "Failed to remove table %s, table not found in HW",
                       list_name);
    }

    /* remove from current interface */
    /* Unbind Table from port */
    if (interface_info &&
        (interface_info->interface == OPS_CLS_INTERFACE_PORT) &&
        ((interface_info->flags & OPS_CLS_INTERFACE_L3ONLY) == 0))
    {
        status = ofproto_sai_class.bundle_set(&(table->ofproto->up), aux, NULL);
        ERRNO_LOG_EXIT(status, "Failed to Remove table %s from port", list_name);
        table->bound_interfaces--;
    }

    /* If table not bound to any interfaces then remove it */
    status = __table_destroy_if_needed(table);
    ERRNO_LOG_EXIT(status, "Failed to destroy table %s", list_name);

    exit:
    SAI_API_TRACE_EXIT_FN();
    return status;
}

static int
__replace(const struct uuid *list_id_orig, const char *list_name_orig,
          struct ops_cls_list *list_new, struct ofproto *ofproto_,
          void *aux, struct ops_cls_interface_info *interface_info,
          enum ops_cls_direction direction,
          struct ops_cls_pd_status *pd_status)
{
    int status = OPS_CLS_STATUS_SUCCESS;
    int rule_id = 0;
    struct sai_classifier_table* table_orig = NULL;
    struct sai_classifier_table* table_new = NULL;
    struct sai_classifier_resources current_use;
    struct ofproto_bundle_settings bundle_settings;

    SAI_API_TRACE_FN();

    ovs_assert(list_id_orig != NULL);
    ovs_assert(list_name_orig != NULL);
    ovs_assert(list_new != NULL);
    ovs_assert(ofproto_ != NULL);
    ovs_assert(interface_info != NULL);
    ovs_assert(pd_status != NULL);

    memset(&current_use, 0 , sizeof(current_use));
    memset(&bundle_settings, 0 , sizeof(bundle_settings));

    /* old table should be configured in HW */
    table_orig = __tables_db_lookup(list_id_orig);
    if (table_orig == NULL) {
        status = OPS_CLS_STATUS_HW_INTERNAL_ERR;
        ERRNO_LOG_EXIT(status, "Failed to find old table %s, table not found in HW", list_name_orig);
    }

    /* Resource accounting */
    if (table_orig->bound_interfaces == 1) {
        current_use.acl_range = -1 * classifier_global_db.per_table_use[table_orig->table_index].acl_range;
        current_use.counters = -1 * classifier_global_db.per_table_use[table_orig->table_index].counters;
        current_use.rules = -1 * classifier_global_db.per_table_use[table_orig->table_index].rules;
    }

    table_new = __tables_db_lookup(&list_new->list_id);

    /* Check resources are available */
    if (table_new == NULL) {
        for (rule_id = 0;rule_id < list_new->num_entries;rule_id++) {
            status = __check_rule_resources_available(&(list_new->entries[rule_id]),
                                                      direction, &current_use);
            if (status) {
                pd_status->entry_id = rule_id;
                pd_status->status_code = OPS_CLS_STATUS_HW_RESOURCE_ERR;
                VLOG_INFO("Insufficient resources for new ACL %s ",
                          list_new->list_name);
                ERRNO_EXIT(status);
            }
        }
    }

    /* Unbind old table */
    /* remove from current interface */
    /* UnBind Table to port */
    if (interface_info &&
        (interface_info->interface == OPS_CLS_INTERFACE_PORT) &&
        ((interface_info->flags & OPS_CLS_INTERFACE_L3ONLY) == 0))
    {
        status = ofproto_sai_class.bundle_set(&(table_orig->ofproto->up),
                                              aux, NULL);
        ERRNO_LOG_EXIT(status, "Failed to Remove table %s from port",
                       list_name_orig);
        table_orig->bound_interfaces--;
    }

    /* If table not bound to any interfaces then remove it */
    status = __table_destroy_if_needed(table_orig);
    ERRNO_LOG_EXIT(status, "Failed to destroy table %s", list_name_orig);

    table_new = __tables_db_lookup(&list_new->list_id);
    if (table_new == NULL) {

        /* Calculate table resources */
        memset(&current_use, 0 , sizeof(current_use));
        for (rule_id = 0;rule_id < list_new->num_entries;rule_id++) {
            status = __check_rule_resources_available(&(list_new->entries[rule_id]),
                                                      direction, &current_use);
            if (status) {
                VLOG_INFO("Insufficient resources for new ACL %s ",
                          list_new->list_name);
                ERRNO_EXIT(status);
            }
        }

        /* Convert rules, Save table in DB */
        status = __write_table(list_new, ofproto_, direction, &table_new);
        ERRNO_EXIT(status);

        __update_resource_usage(table_new->table_index, &current_use);
        table_new = __tables_db_add(table_new);
    }

    /* Bind new table to port */
    if (interface_info &&
        (interface_info->interface == OPS_CLS_INTERFACE_PORT) &&
        ((interface_info->flags & OPS_CLS_INTERFACE_L3ONLY) == 0))
    {
        status = ofproto_sai_class.bundle_set(&(table_new->ofproto->up),
                                              aux, &bundle_settings);
        ERRNO_LOG_EXIT(status, "Failed to apply table %s on port",
                       list_new->list_name);
        table_new->bound_interfaces++;
    }

    exit:
    SAI_API_TRACE_EXIT_FN();
    return status;
}

static int
__list_update(struct ops_cls_list *list,
              struct ops_cls_pd_list_status *pd_status)
{
    int status = OPS_CLS_STATUS_SUCCESS;
    int rule_id = 0;
    struct sai_classifier_table* table = NULL;
    struct sai_classifier_resources current_use;
    struct rule *ofrule;
    enum ops_cls_direction direction;

    SAI_API_TRACE_FN();

    ovs_assert(list != NULL);
    ovs_assert(pd_status != NULL);

    /* table should be configured in HW */
    table = __tables_db_lookup(&list->list_id);
    if (table == NULL) {
        status = OPS_CLS_STATUS_HW_INTERNAL_ERR;
        ERRNO_LOG_EXIT(status,
                       "Failed to update table %s, table not found in HW",
                       list->list_name);
    }

    /* Resource accounting - check if there's place for new list */
    current_use.acl_range = -1 * classifier_global_db.per_table_use[table->table_index].acl_range;
    current_use.counters = -1 * classifier_global_db.per_table_use[table->table_index].counters;
    current_use.rules = -1 * classifier_global_db.per_table_use[table->table_index].rules;

    direction = (strcmp(table->ofproto->up.type, SAI_TYPE_IACL) == 0) ? OPS_CLS_DIRECTION_IN : OPS_CLS_DIRECTION_OUT;

    for (rule_id = 0;rule_id < list->num_entries;rule_id++) {
        status = __check_rule_resources_available(&(list->entries[rule_id]),
                                                  direction, &current_use);
        if (status) {
            pd_status->entry_id = rule_id;
            pd_status->status_code = OPS_CLS_STATUS_HW_RESOURCE_ERR;
            VLOG_INFO("Insufficient resources for ACL update %s ",
                      list->list_name);
            ERRNO_EXIT(status);
        }
    }

    /* TODO :
     * List update should have provided a rule information
     * Since this is list update we would like to preserve
     *  - counters for unchanged rules
     *  - unchanged rules (to keep security rules in place while not edited)
     *
     *  For now, it seems we need to change ASIC plugin API so we would have the following
     *  or some of them available for us:
     *
     *  - Rule UUID
     *  - Rule priority (or sequence number)
     *  - Per rule action (New, replace, delete)
     *
     *  Current implementation is Naive - removes rules and installs the new list
     */

    /* remove rules from Table */
    for (rule_id = table->active_rules - 1;rule_id >= 0;rule_id--) {
        ofproto_sai_class.rule_delete(table->rules[rule_id].of_rule);
        ofproto_sai_class.rule_destruct(table->rules[rule_id].of_rule);
        ofproto_sai_class.rule_dealloc(table->rules[rule_id].of_rule);
        table->active_rules--;
        table->rules[rule_id].of_rule = NULL;
        table->rules[rule_id].count = false;
    }
    /* Write new rules to table */
    for (rule_id = 0;rule_id < list->num_entries;rule_id++) {
        ofrule = ofproto_sai_class.rule_alloc();
        if (!ofrule) {
            status = OPS_CLS_STATUS_HW_INTERNAL_ERR;
            ERRNO_EXIT(status);
        }
        __rule_base_init(ofrule, &(table->ofproto->up));
        table->rules[rule_id].of_rule = ofrule;
        /* Convert rule fields */
        __acl_entry_to_classifier_rule(rule_id, &(list->entries[rule_id]),
                                       &(table->rules[rule_id]));
        status = ofproto_sai_class.rule_construct(ofrule);
        ERRNO_LOG_EXIT(status, "Failed to construct rule index %d in Table %s",
                       rule_id, list->list_name);
        ofproto_sai_class.rule_insert(ofrule, NULL, 0);
    }

    exit:
    SAI_API_TRACE_EXIT_FN();
    return status;
}

static int
__statistics_get(const struct uuid *list_id, const char *list_name,
                 enum ops_cls_type list_type, struct ofproto *ofproto,
                 void *aux, struct ops_cls_interface_info *interface_info,
                 enum ops_cls_direction direction,
                 struct ops_cls_statistics *statistics,
                 int num_entries, struct ops_cls_pd_list_status  *status)
{
    SAI_API_TRACE_FN();
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();
    SAI_API_TRACE_EXIT_FN();
    return OPS_CLS_STATUS_SUCCESS;
}

static int
__statistics_clear(const struct uuid *list_id, const char *list_name,
                   enum ops_cls_type list_type, struct ofproto *ofproto,
                   void *aux, struct ops_cls_interface_info *interface_info,
                   enum ops_cls_direction direction,
                   struct ops_cls_pd_list_status *status)
{
    SAI_API_TRACE_FN();
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();
    SAI_API_TRACE_EXIT_FN();
    return OPS_CLS_STATUS_SUCCESS;
}

static int
__statistics_clear_all(struct ops_cls_pd_list_status *status)
{
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();
    SAI_API_TRACE_EXIT_FN();
    return OPS_CLS_STATUS_SUCCESS;
}

static int
__log_pkt_register_cb(void (*callback_handler)(struct acl_log_info *))
{
    SAI_API_TRACE_FN();
    SAI_API_TRACE_NOT_IMPLEMENTED_FN();
    SAI_API_TRACE_EXIT_FN();
    return OPS_CLS_STATUS_SUCCESS;
}

/*
 * Initialize Classifier module.
 */
static void
__classifier_init(void)
{
    VLOG_INFO("Initializing Classifier");

    /* Tables hash map */
    hmap_init(&tables_map);
}

/*
 * De-initialize Classifier module.
 */
static void
__classifier_deinit(void)
{
    VLOG_INFO("De-initializing Classifier");
    hmap_destroy(&tables_map);
}

int
register_classifier_sai_plugin()
{
    return (register_plugin_extension(&cls_sai_extension));
}

DEFINE_GENERIC_CLASS(struct classifier_class, classifier) = {
    .init = __classifier_init,
    .deinit = __classifier_deinit,
};

DEFINE_GENERIC_CLASS_GETTER(struct classifier_class, classifier);
