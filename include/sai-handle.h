/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#ifndef SAI_HANDLE_H
#define SAI_HANDLE_H 1

#include <inttypes.h>

#define HANDLE_INITIALIZAER  { 0 }

typedef struct handle {
    uint64_t data;
} handle_t;

#define HANDLE_EQ(h1, h2)    ((h1)->data == (h2)->data)

#endif /* sai-handle.h */
