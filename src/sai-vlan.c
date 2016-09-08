/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#include <vlan-bitmap.h>
#include <ofproto/ofproto.h>
#include <hmap.h>
#include <hash.h>

#include <sai-log.h>
#include <sai-api-class.h>
#include <sai-port.h>
#include <sai-vlan.h>

VLOG_DEFINE_THIS_MODULE(sai_vlan);

#define VLAN_MEMBER_PACK(_hw_id, _vlan) \
    ((((uint64_t) _hw_id) << 32) | (_vlan & 0xFFF))

#define VLAN_MEMBER_GET_PORT(_member) \
    ((uint32_t) (_member >> 32))

#define VLAN_MEMBER_GET_VLAN(_member) \
    ((sai_vlan_id_t) (_member & 0xFFF))

static int __vlan_port_set(sai_vlan_id_t, uint32_t, sai_vlan_tagging_mode_t,
                           bool);
static int __trunks_port_set(const unsigned long *, uint32_t, bool);

static struct hmap all_vlan_members = HMAP_INITIALIZER(&all_vlan_members);

struct vlan_member_entry {
    struct hmap_node hmap_node;
    uint64_t vlan_member;
    sai_object_id_t oid;
    sai_vlan_tagging_mode_t mode;
};

/*
 * Find VLAN member entry in hash map.
 *
 * @param[in] vlan_member_hmap    - Hash map.
 * @param[in] vlan_member         - Router interface handle used as map key.
 *
 * @return Pointer to VLAN member entry or NULL if entry not found.
 */
static struct vlan_member_entry*
__vlan_member_entry_hmap_find(struct hmap *vlan_member_hmap,
                              uint64_t vlan_member)
{
    struct vlan_member_entry* vlan_member_entry = NULL;

    HMAP_FOR_EACH_WITH_HASH(vlan_member_entry, hmap_node,
                            hash_uint64(vlan_member), vlan_member_hmap) {
        if (vlan_member_entry->vlan_member == vlan_member) {
            return vlan_member_entry;
        }
    }

    return NULL;
}

/*
 * Add VLAN member entry to hash map.
 *
 * @param[in] vlan_member_hmap        - Hash map.
 * @param[in] vlan_member_entry       - VLAN member entry.
 */
static void
__vlan_member_entry_hmap_add(struct hmap *vlan_member_hmap,
                             const struct vlan_member_entry* vlan_member_entry)
{
    struct vlan_member_entry *vlan_member_entry_int = NULL;

    ovs_assert(!__vlan_member_entry_hmap_find(vlan_member_hmap,
                                              vlan_member_entry->vlan_member));

    vlan_member_entry_int = xzalloc(sizeof(*vlan_member_entry_int));
    memcpy(vlan_member_entry_int, vlan_member_entry,
           sizeof(*vlan_member_entry_int));

    hmap_insert(vlan_member_hmap, &vlan_member_entry_int->hmap_node,
                hash_uint64(vlan_member_entry_int->vlan_member));
}

/*
 * Delete VLAN member entry from hash map.
 *
 * @param[in] vlan_member_hmap   - Hash map.
 * @param[in] vlan_member        - VLAN member used as map key.
 */
static void
__vlan_member_entry_hmap_del(struct hmap *vlan_member_hmap,
                             uint64_t vlan_member)
{
    struct vlan_member_entry* vlan_member_entry =
            __vlan_member_entry_hmap_find(vlan_member_hmap,
                                          vlan_member);
    if (vlan_member_entry) {
        hmap_remove(vlan_member_hmap, &vlan_member_entry->hmap_node);
        free(vlan_member_entry);
    }
}

/*
 * Initialize VLANs.
 */
void
__vlan_init(void)
{
    VLOG_INFO("Initializing VLANs");
}

/*
 * De-initialize VLANs.
 */
void
__vlan_deinit(void)
{
    VLOG_INFO("De-initializing VLANs");
}

/*
 * Adds port to access vlan.
 * @param[in] vid VLAN id.
 * @param[in] hw_id port label id.
 * @return 0, sai error converted to errno otherwise.
 */
int
__vlan_access_port_add(sai_vlan_id_t vid, uint32_t hw_id)
{
    return __vlan_port_set(vid, hw_id, SAI_VLAN_PORT_UNTAGGED, true);
}

/*
 * Removes port from access vlan and sets PVID to default.
 * @param[in] vid VLAN id.
 * @param[in] hw_id port label id.
 * @return 0, sai error converted to errno otherwise.
 */
int
__vlan_access_port_del(sai_vlan_id_t vid, uint32_t hw_id)
{
    int status = 0;

    /* Mode doesn't matter when port is removed from vlan. */
    status = __vlan_port_set(vid, hw_id, SAI_VLAN_PORT_UNTAGGED, false);
    ERRNO_EXIT(status);

    status = ops_sai_port_pvid_set(hw_id, OPS_SAI_PORT_DEFAULT_PVID);
    ERRNO_EXIT(status);

exit:
    return status;
}

/*
 * Adds port to trunks.
 * @param[in] trunks vlan bitmap.
 * @param[in] hw_id port label id.
 * @return 0, sai error converted to errno otherwise.
 */
int
__vlan_trunks_port_add(const unsigned long *trunks, uint32_t hw_id)
{
    return __trunks_port_set(trunks, hw_id, true);
}

/*
 * Removes port from trunks.
 * @param[in] hw_id port label id.
 * @param[in] trunks vlan bitmap.
 * @return 0, sai error converted to errno otherwise.
 */
int
__vlan_trunks_port_del(const unsigned long *trunks, uint32_t hw_id)
{
    return __trunks_port_set(trunks, hw_id, false);
}

/*
 * Creates or destroys vlan.
 * @param[in] vid VLAN id.
 * @param[in] add boolean which says if vlan should be added or removed.
 * @return 0, sai error converted to errno otherwise.
 */
int
__vlan_set(int vid, bool add)
{
    sai_status_t status = SAI_STATUS_SUCCESS;
    const struct ops_sai_api_class *sai_api = ops_sai_api_get_instance();

    if (add) {
        status = sai_api->vlan_api->create_vlan(vid);
    } else {
        status = sai_api->vlan_api->remove_vlan(vid);
    }
    SAI_ERROR_LOG_EXIT(status, "Failed to %s vlan vid %d",
                       add ? "create" : "remove", vid);

exit:
    return SAI_ERROR_2_ERRNO(status);
}

/*
 * Sets vlan to port.
 * @param[in] vid VLAN id.
 * @param[in] hw_id port label id.
 * @param[in] mode tagging mode: tagged/untagged.
 * @param[in] add boolean which says if port should be added or removed to/from
 * vlan.
 * @return 0, sai error converted to errno otherwise.
 */
static int
__vlan_port_set(sai_vlan_id_t vid, uint32_t hw_id,
                sai_vlan_tagging_mode_t mode, bool add)
{
    sai_status_t status = SAI_STATUS_SUCCESS;
    uint64_t vlan_member = 0;
    sai_object_id_t vlan_member_oid = SAI_NULL_OBJECT_ID;
    sai_attribute_t vlan_member_attribs[3] = { };
    struct vlan_member_entry vlan_member_entry = { };
    struct vlan_member_entry *vlan_member_entry_ptr = NULL;
    const struct ops_sai_api_class *sai_api = ops_sai_api_get_instance();

    vlan_member = VLAN_MEMBER_PACK(hw_id, vid);

    if (add) {
        VLOG_INFO("Adding port to VLAN (port: %u, vlan: %u, mode: %d)",
                  hw_id, vid, mode);

        vlan_member_attribs[0].id = SAI_VLAN_MEMBER_ATTR_VLAN_ID;
        vlan_member_attribs[0].value.u16 = vid;
        vlan_member_attribs[1].id = SAI_VLAN_MEMBER_ATTR_PORT_ID;
        vlan_member_attribs[1].value.oid = ops_sai_api_port_map_get_oid(hw_id);
        vlan_member_attribs[2].id = SAI_VLAN_MEMBER_ATTR_TAGGING_MODE;
        vlan_member_attribs[2].value.s32 = mode;

        /* VLAN member entry mode was changed */
        vlan_member_entry_ptr = __vlan_member_entry_hmap_find(&all_vlan_members,
                                                              vlan_member);
        if (vlan_member_entry_ptr && vlan_member_entry_ptr->mode != mode) {
            if (__vlan_port_set(vid, hw_id, vlan_member_entry_ptr->mode,
                                false)) {
                status = SAI_STATUS_FAILURE;
                SAI_ERROR_EXIT(status);
            }
        }

        status = sai_api->vlan_api->create_vlan_member(&vlan_member_oid,
                                                       ARRAY_SIZE(vlan_member_attribs),
                                                       vlan_member_attribs);
        SAI_ERROR_LOG_EXIT(status,
                           "Failed to add port to VLAN (port: %u, vlan: %u)",
                           hw_id, vid);

        vlan_member_entry.vlan_member = vlan_member;
        vlan_member_entry.oid = vlan_member_oid;
        vlan_member_entry.mode = mode;

        __vlan_member_entry_hmap_add(&all_vlan_members, &vlan_member_entry);
    } else {
        VLOG_INFO("Removing port from VLAN (port: %u, vlan: %u, mode: %d)",
                  hw_id, vid, mode);

        vlan_member_entry_ptr = __vlan_member_entry_hmap_find(&all_vlan_members,
                                                              vlan_member);
        if (!vlan_member_entry_ptr) {
            status = SAI_STATUS_SUCCESS;
            goto exit;
        }

        status = sai_api->vlan_api->remove_vlan_member(vlan_member_entry_ptr->oid);
        if (status != SAI_STATUS_ITEM_NOT_FOUND) {
            SAI_ERROR_LOG_EXIT(status,
                               "Failed to remove port from VLAN "
                               "(port: %u, vlan: %u)",
                               hw_id, vid);
        }

        __vlan_member_entry_hmap_del(&all_vlan_members, vlan_member);
    }

    if (add && (SAI_VLAN_PORT_UNTAGGED != mode)) {
        goto exit;
    }

    /* No need to convert return code - already errno value. */
    return ops_sai_port_pvid_set(hw_id, vid);

exit:
    return SAI_ERROR_2_ERRNO(status);
}

/*
 * Sets trunks to port.
 * @param[in] trunks vlan bitmap.
 * @param[in] hw_id port label id.
 * @param[in] add boolean which says if port should be added or removed to/from
 * vlan.
 * @return 0, sai error converted to errno otherwise.
 */
static int
__trunks_port_set(const unsigned long *trunks, uint32_t hw_id, bool add)
{
    int vid = 0;
    int status = 0;

    NULL_PARAM_LOG_ABORT(trunks);

    BITMAP_FOR_EACH_1(vid, VLAN_BITMAP_SIZE, trunks) {
        status = __vlan_port_set(vid, hw_id, SAI_VLAN_PORT_TAGGED, add);
        ERRNO_LOG_EXIT(status, "Failed to %s trunks", add ? "add" : "remove");
    }

exit:
    return status;
}

DEFINE_GENERIC_CLASS(struct vlan_class, vlan) = {
        .init = __vlan_init,
        .access_port_add = __vlan_access_port_add,
        .access_port_del = __vlan_access_port_del,
        .trunks_port_add = __vlan_trunks_port_add,
        .trunks_port_del = __vlan_trunks_port_del,
        .set = __vlan_set,
        .deinit = __vlan_deinit,
};

DEFINE_GENERIC_CLASS_GETTER(struct vlan_class, vlan);
