/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#ifndef SAI_VENDOR_PORT_H
#define SAI_VENDOR_PORT_H 1

#include <sai-common.h>

DECLARE_VENDOR_CLASS_GETTER(struct port_class, port);

#define ops_sai_port_class() (CLASS_VENDOR_GETTER(port)())

#endif /* sai-vendor-port.h */
