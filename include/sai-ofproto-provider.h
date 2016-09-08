/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.  
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#ifndef OFPROTO_SAI_PROVIDER_H
#define OFPROTO_SAI_PROVIDER_H 1

#include <seq.h>
#include <coverage.h>
#include <hmap.h>
#include <vlan-bitmap.h>
#include <socket-util.h>
#include <ofproto/ofproto-provider.h>
#include <ofproto/bond.h>
#include <ofproto/tunnel.h>

#include <vswitch-idl.h>
#include <openswitch-idl.h>
#include <ofproto/ofproto-provider.h>

const struct ofproto_class ofproto_sai_class;

#define SAI_TYPE_IACL           "iACL"
#define SAI_TYPE_EACL           "eACL"

struct ofproto_sai {
    struct ofproto up;
    struct hmap_node all_ofproto_sai_node;      /* In 'all_ofproto_dpifs'. */
    struct hmap bundles;        /* Contains "struct ofbundle"s. */
    struct sset ports;          /* Set of standard port names. */
    struct sset ghost_ports;    /* Ports with no datapath port. */
    handle_t vrid;
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

    /* L3 interface */
    struct {
        bool created;
        bool enabled;
        handle_t handle; /* VLAN or port ID */
        handle_t rifid;
        bool is_loopback;
    } router_intf;

    /* L3 IP addresses */
    char *ipv4_primary;
    char *ipv6_primary;
    struct hmap ipv4_secondary;
    struct hmap ipv6_secondary;

    /* Local routes entries */
    struct hmap local_routes;
    /* Neighbor entries */
    struct hmap neighbors;

    struct {
        bool cache_config; /* Specifies if config should be cached */
        struct ofproto_bundle_settings *config;
        struct hmap local_routes;
    } config_cache;
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

/*
 * Cast netdev ofproto to ofproto_sai.
 */
inline struct ofproto_sai *
ofproto_sai_cast(const struct ofproto *ofproto)
{
    ovs_assert(ofproto);
    ovs_assert(ofproto->ofproto_class == &ofproto_sai_class);
    return CONTAINER_OF(ofproto, struct ofproto_sai, up);
}

void ofproto_sai_register(void);
int ofproto_sai_bundle_enable(const char *);
int ofproto_sai_bundle_disable(const char *);

#endif /* sai-ofproto-provider.h */
