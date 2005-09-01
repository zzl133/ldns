/*
 * util.h
 * util.c header file 
 * in ldns
 * (c) 2005 NLnet Labs
 *
 * See the file LICENSE for the license
 *
 */

#ifndef _DRILL_UTIL_H_
#define _DRILL_UTIL_H_
#include <ldns/dns.h>

/**
 * return a address rdf, either A or AAAA 
 * NULL if anything goes wrong
 */
ldns_rdf * ldns_rdf_new_addr_frm_str(char *);

/**
 * print all the ds of the keys in the packet
 */
void print_ds_of_keys(ldns_pkt *p);

/**
 * Alloc some memory, with error checking
 */
void *xmalloc(size_t s);

/** 
 * Realloc some memory, with error checking
 */
void *xrealloc(void *p, size_t s);

/**
 * Free the data
 */
void xfree(void *q);
#endif /* _DRILL_UTIL_H_ */