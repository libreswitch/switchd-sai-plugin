/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.  
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#ifndef SAI_API_CLASS_H
#define SAI_API_CLASS_H 1

#include <sai.h>

#define SAI_PORTS_MAX (64)
#define SAI_MAX_LANES (4)

struct ops_sai_api_class {
    sai_switch_api_t *switch_api;
    sai_port_api_t *port_api;
    sai_vlan_api_t *vlan_api;
    sai_hostif_api_t *host_interface_api;
    bool initialized;
};

void ops_sai_api_init(void);
int ops_sai_api_uninit(void);
const struct ops_sai_api_class *ops_sai_api_get_instance(void);
sai_object_id_t ops_sai_api_hw_id2port_id(uint32_t);

#endif /* sai-api-class.h */
