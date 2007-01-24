/*
 * chasetrace.c
 * Where all the hard work concerning chasing
 * and tracing is done
 * (c) 2005, 2006 NLnet Labs
 *
 * See the file LICENSE for the license
 *
 */

#include "drill.h"
#include <ldns/ldns.h>

/**
 * trace down from the root to name
 */

/* same naive method as in drill0.9 
 * We resolver _ALL_ the names, which is ofcourse not needed
 * We _do_ use the local resolver to do that, so it still is
 * fast, but it can be made to run much faster
 */
ldns_pkt *
do_trace(ldns_resolver *local_res, ldns_rdf *name, ldns_rr_type t,
		ldns_rr_class c)
{
	ldns_resolver *res;
	ldns_pkt *p;
	ldns_rr_list *new_nss_a;
	ldns_rr_list *new_nss_aaaa;
	ldns_rr_list *final_answer;
	ldns_rr_list *new_nss;
	ldns_rr_list *hostnames;
	ldns_rr_list *ns_addr;
	uint16_t loop_count;
	ldns_rdf *pop; 
	ldns_status status;
	size_t i;
	
	loop_count = 0;
	new_nss_a = NULL;
	new_nss_aaaa = NULL;
	new_nss = NULL;
	ns_addr = NULL;
	final_answer = NULL;
	p = ldns_pkt_new();
	res = ldns_resolver_new();

	if (!p || !res) {
                error("Memory allocation failed");
                return NULL;
        }

	/* transfer some properties of local_res to res,
	 * because they were given on the commandline */
	ldns_resolver_set_ip6(res, 
			ldns_resolver_ip6(local_res));
	ldns_resolver_set_port(res, 
			ldns_resolver_port(local_res));
	ldns_resolver_set_debug(res, 
			ldns_resolver_debug(local_res));
	ldns_resolver_set_dnssec(res, 
			ldns_resolver_dnssec(local_res));
	ldns_resolver_set_fail(res, 
			ldns_resolver_fail(local_res));
	ldns_resolver_set_usevc(res, 
			ldns_resolver_usevc(local_res));
	ldns_resolver_set_random(res, 
			ldns_resolver_random(local_res));
	ldns_resolver_set_recursive(res, false);

	/* setup the root nameserver in the new resolver */
	status = ldns_resolver_push_nameserver_rr_list(res, global_dns_root);
	if (status != LDNS_STATUS_OK) {
		fprintf(stderr, "Error adding root servers to resolver: %s\n", ldns_get_errorstr_by_id(status));
		ldns_rr_list_print(stdout, global_dns_root);
		return NULL;
	}

	/* this must be a real query to local_res */
	status = ldns_resolver_send(&p, res, ldns_dname_new_frm_str("."), LDNS_RR_TYPE_NS, c, 0);
	/* p can still be NULL */


	if (ldns_pkt_empty(p)) {
		warning("No root server information received");
	} 
	
	if (status == LDNS_STATUS_OK) {
		if (!ldns_pkt_empty(p)) {
			drill_pkt_print(stdout, local_res, p);
		}
	} else {
		error("cannot use local resolver");
		return NULL;
	}

	status = ldns_resolver_send(&p, res, name, t, c, 0);

	while(status == LDNS_STATUS_OK && 
	      ldns_pkt_reply_type(p) == LDNS_PACKET_REFERRAL) {

		if (!p) {
			/* some error occurred, bail out */
			return NULL;
		}

		new_nss_a = ldns_pkt_rr_list_by_type(p,
				LDNS_RR_TYPE_A, LDNS_SECTION_ADDITIONAL);
		new_nss_aaaa = ldns_pkt_rr_list_by_type(p,
				LDNS_RR_TYPE_AAAA, LDNS_SECTION_ADDITIONAL);
		new_nss = ldns_pkt_rr_list_by_type(p,
				LDNS_RR_TYPE_NS, LDNS_SECTION_AUTHORITY);

		if (verbosity != -1) {
			ldns_rr_list_print(stdout, new_nss);
		}
		/* checks itself for verbosity */
		drill_pkt_print_footer(stdout, local_res, p);
		
		/* remove the old nameserver from the resolver */
		while((pop = ldns_resolver_pop_nameserver(res))) { /* do it */ }

		/* also check for new_nss emptyness */

		if (!new_nss_aaaa && !new_nss_a) {
			/* 
			 * no nameserver found!!! 
			 * try to resolve the names we do got 
			 */
			for(i = 0; i < ldns_rr_list_rr_count(new_nss); i++) {
				/* get the name of the nameserver */
				pop = ldns_rr_rdf(ldns_rr_list_rr(new_nss, i), 0);
				if (!pop) {
					break;
				}

				ldns_rr_list_print(stdout, new_nss);
				ldns_rdf_print(stdout, pop);
				/* retrieve it's addresses */
				ns_addr = ldns_rr_list_cat_clone(ns_addr,
					ldns_get_rr_list_addr_by_name(local_res, pop, c, 0));
			}

			if (ns_addr) {
				if (ldns_resolver_push_nameserver_rr_list(res, ns_addr) != 
						LDNS_STATUS_OK) {
					error("Error adding new nameservers");
					ldns_pkt_free(p); 
					return NULL;
				}
				ldns_rr_list_free(ns_addr);
			} else {
				ldns_rr_list_print(stdout, ns_addr);
				error("Could not find the nameserver ip addr; abort");
				ldns_pkt_free(p);
				return NULL;
			}
		}

		/* add the new ones */
		if (new_nss_aaaa) {
			if (ldns_resolver_push_nameserver_rr_list(res, new_nss_aaaa) != 
					LDNS_STATUS_OK) {
				error("adding new nameservers");
				ldns_pkt_free(p); 
				return NULL;
			}
		}
		if (new_nss_a) {
			if (ldns_resolver_push_nameserver_rr_list(res, new_nss_a) != 
					LDNS_STATUS_OK) {
				error("adding new nameservers");
				ldns_pkt_free(p); 
				return NULL;
			}
		}

		if (loop_count++ > 20) {
			/* unlikely that we are doing something usefull */
			error("Looks like we are looping");
			ldns_pkt_free(p); 
			return NULL;
		}
		
		status = ldns_resolver_send(&p, res, name, t, c, 0);
		new_nss_aaaa = NULL;
		new_nss_a = NULL;
		ns_addr = NULL;
	}

	status = ldns_resolver_send(&p, res, name, t, c, 0);

	if (!p) {
		return NULL;
	}

	hostnames = ldns_get_rr_list_name_by_addr(local_res, 
			ldns_pkt_answerfrom(p), 0, 0);

	new_nss = ldns_pkt_authority(p);
	final_answer = ldns_pkt_answer(p);

	if (verbosity != -1) {
		ldns_rr_list_print(stdout, final_answer);
		ldns_rr_list_print(stdout, new_nss);

	}
	drill_pkt_print_footer(stdout, local_res, p);
	ldns_pkt_free(p); 
	return NULL;
}



/**
 * Chase the given rr to a known and trusted key
 *
 * Based on drill 0.9
 *
 * the last argument prev_key_list, if not null, and type == DS, then the ds
 * rr list we have must all be a ds for the keys in this list
 */
ldns_status
do_chase(ldns_resolver *res, ldns_rdf *name, ldns_rr_type type, ldns_rr_class c,
		ldns_rr_list *trusted_keys, ldns_pkt *pkt_o, uint16_t qflags, ldns_rr_list *prev_key_list)
{
	ldns_rr_list *rrset = NULL;
	ldns_status result;
	
	ldns_rr_list *sigs;
	ldns_rr *cur_sig;
	uint16_t sig_i;
	ldns_rr_list *keys;
	ldns_rr_list *nsec_rrs = NULL;
	ldns_rr_list *nsec_rr_sigs = NULL;

	uint16_t ksk_i;
	uint16_t ksk_sig_i;
	ldns_rr *ksk_sig = NULL;

	uint16_t key_i;
	uint16_t tkey_i;
	ldns_pkt *pkt;
	size_t i,j;
/*	ldns_rr_list *tmp_list;*/
	bool key_matches_ds;
	

	ldns_lookup_table *lt;
	const ldns_rr_descriptor *descriptor;
	
	descriptor = ldns_rr_descript(type);

	ldns_dname2canonical(name);
	
	pkt = ldns_pkt_clone(pkt_o);
	if (!name) {
		mesg("No name to chase");
		ldns_pkt_free(pkt);
		return LDNS_STATUS_EMPTY_LABEL;
	}
	if (verbosity != -1) {
		printf(";; Chasing: ");
			ldns_rdf_print(stdout, name);
			if (descriptor && descriptor->_name) {
				printf(" %s\n", descriptor->_name);
			} else {
				printf(" type %d\n", type);
			}
	}

	if (!trusted_keys || ldns_rr_list_rr_count(trusted_keys) < 1) {
		warning("No trusted keys specified");
	}
	
	if (pkt) {
		rrset = ldns_pkt_rr_list_by_name_and_type(pkt,
				name,
				type,
				LDNS_SECTION_ANSWER
				);
		if (!rrset) {
			/* nothing in answer, try authority */
			rrset = ldns_pkt_rr_list_by_name_and_type(pkt,
					name,
					type,
					LDNS_SECTION_AUTHORITY
					);
		}
	} else {
		/* no packet? */
		return LDNS_STATUS_MEM_ERR;
	}
	
	if (!rrset) {
		/* not found in original packet, try again */
		ldns_pkt_free(pkt);
		pkt = NULL;
		pkt = ldns_resolver_query(res, name, type, c, qflags);
		
		if (!pkt) {
			return LDNS_STATUS_NETWORK_ERR;
		}
		if (verbosity >= 5) {
			ldns_pkt_print(stdout, pkt);
		}
		
		rrset =	ldns_pkt_rr_list_by_name_and_type(pkt,
				name,
				type,
				LDNS_SECTION_ANSWER
				);
	}

	sigs = ldns_pkt_rr_list_by_name_and_type(pkt,
			name,
			LDNS_RR_TYPE_RRSIG,
			LDNS_SECTION_ANY_NOQUESTION
			);
	/* these can contain sigs for other rrsets too! */
	
	if (rrset) {
		for (sig_i = 0; sig_i < ldns_rr_list_rr_count(sigs); sig_i++) {
			cur_sig = ldns_rr_clone(ldns_rr_list_rr(sigs, sig_i));
			if (ldns_rdf2native_int16(ldns_rr_rrsig_typecovered(cur_sig)) == type) {
			
			keys = ldns_pkt_rr_list_by_name_and_type(pkt,
					ldns_rr_rdf(cur_sig, 7),
					LDNS_RR_TYPE_DNSKEY,
					LDNS_SECTION_ANY_NOQUESTION
					);
			
			if (verbosity != -1) {
				printf(";; Data set: ");
				ldns_rdf_print(stdout, name);

				lt = ldns_lookup_by_id(ldns_rr_classes, c);
				if (lt) {
					printf("\t%s\t", lt->name);
				} else {
					printf("\tCLASS%d\t", c);
				}

				if (descriptor && descriptor->_name) {
					printf("%s\t", descriptor->_name);
				} else {
					/* exceptions for qtype */
					if (type == 251) {
						printf("IXFR ");
					} else if (type == 252) {
						printf("AXFR ");
					} else if (type == 253) {
						printf("MAILB ");
					} else if (type == 254) {
						printf("MAILA ");
					} else if (type == 255) {
						printf("ANY ");
					} else {
						printf("TYPE%d\t", type);
					}
				}
				
				printf("\n");
				printf(";; Signed by: ");
				ldns_rdf_print(stdout, ldns_rr_rdf(cur_sig, 7));
				printf("\n");
				if (type == LDNS_RR_TYPE_DS && prev_key_list) {
					for (j = 0; j < ldns_rr_list_rr_count(rrset); j++) {
						key_matches_ds = false;
						for (i = 0; i < ldns_rr_list_rr_count(prev_key_list); i++) {
							if (ldns_rr_compare_ds(ldns_rr_list_rr(prev_key_list, i),
									       ldns_rr_list_rr(rrset, j))) {
								key_matches_ds = true;
							}
						}
						if (!key_matches_ds) {
							/* For now error */
							fprintf(stderr, ";; error no DS for key\n");
							return LDNS_STATUS_ERR;
						}
					}
				}
			}

			if (!keys) {
				ldns_pkt_free(pkt);
				pkt = NULL;
				pkt = ldns_resolver_query(res,
						ldns_rr_rdf(cur_sig, 7),
						LDNS_RR_TYPE_DNSKEY, c, qflags);
				if (!pkt) {
					ldns_rr_list_deep_free(rrset);
					ldns_rr_list_deep_free(sigs);
					return LDNS_STATUS_NETWORK_ERR;
				}

				if (verbosity >= 5) {
					ldns_pkt_print(stdout, pkt);
				}
				
				keys = ldns_pkt_rr_list_by_name_and_type(pkt,
						ldns_rr_rdf(cur_sig, 7),
						LDNS_RR_TYPE_DNSKEY,
						LDNS_SECTION_ANY_NOQUESTION
						);
			}
			if(!keys) {
				mesg("No key for data found in that zone!");
				ldns_rr_list_deep_free(rrset);
				ldns_rr_list_deep_free(sigs);
				ldns_pkt_free(pkt);
				ldns_rr_free(cur_sig);
				return LDNS_STATUS_CRYPTO_NO_DNSKEY;
			} else {
				result = LDNS_STATUS_ERR;
				for (key_i = 0; key_i < ldns_rr_list_rr_count(keys); key_i++) {
					/* only check matching keys */
					if (ldns_calc_keytag(ldns_rr_list_rr(keys, key_i))
					    ==
					    ldns_rdf2native_int16(ldns_rr_rrsig_keytag(cur_sig))
					   ) {
						result = ldns_verify_rrsig(rrset, cur_sig, ldns_rr_list_rr(keys, key_i));
						if (result == LDNS_STATUS_OK) {
							for (tkey_i = 0; tkey_i < ldns_rr_list_rr_count(trusted_keys); tkey_i++) {
								if (ldns_rr_compare_ds(ldns_rr_list_rr(keys, key_i),
										   ldns_rr_list_rr(trusted_keys, tkey_i)
										  )) {
									mesg("Key is trusted");
									ldns_rr_list_deep_free(rrset);
									ldns_rr_list_deep_free(sigs);
									ldns_rr_list_deep_free(keys);
									ldns_pkt_free(pkt);
									ldns_rr_free(cur_sig);
									return LDNS_STATUS_OK;
								}
							}
							/* apparently the key is not trusted, so it must either be signed itself or have a DS in the parent */
							if (type == LDNS_RR_TYPE_DNSKEY && ldns_rdf_compare(name, ldns_rr_rdf(cur_sig, 7)) == 0) {
								/* check the other signatures, there might be a trusted KSK here */
								for (ksk_sig_i = 0; ksk_sig_i < ldns_rr_list_rr_count(sigs); ksk_sig_i++) {
									ksk_sig = ldns_rr_list_rr(sigs, ksk_sig_i);
									if (ldns_rdf2native_int16(ldns_rr_rrsig_keytag(ksk_sig)) !=
									    ldns_calc_keytag(ldns_rr_list_rr(keys, key_i))) {
										for (ksk_i = 0; ksk_i < ldns_rr_list_rr_count(keys); ksk_i++) {
											if (ldns_rdf2native_int16(ldns_rr_rrsig_keytag(ksk_sig)) ==
											    ldns_calc_keytag(ldns_rr_list_rr(keys, ksk_i))) {
												result = ldns_verify_rrsig(rrset, cur_sig, ldns_rr_list_rr(keys, key_i));
												if (result == LDNS_STATUS_OK) {
													for (tkey_i = 0; tkey_i < ldns_rr_list_rr_count(trusted_keys); tkey_i++) {
														if (ldns_rr_compare_ds(ldns_rr_list_rr(keys, ksk_i),
																   ldns_rr_list_rr(trusted_keys, tkey_i)
																  )) {
															if (verbosity > 1) {
																mesg("Key is signed by trusted KSK");
															}
															ldns_rr_list_deep_free(rrset);
															ldns_rr_list_deep_free(sigs);
															ldns_rr_list_deep_free(keys);
															ldns_pkt_free(pkt);
															ldns_rr_free(cur_sig);
															return LDNS_STATUS_OK;
														}
													}
												}
											}
										}
										
									}
								}

								/* okay now we are looping in a selfsigned key, find the ds or bail */
								/* there can never be a DS for the root label unless it has been given,
								 * so we can't chase that */
								if (ldns_rdf_size(ldns_rr_rdf(cur_sig, 7)) > 1) {
									result = do_chase(res, ldns_rr_rdf(cur_sig, 7), LDNS_RR_TYPE_DS, c, trusted_keys, pkt, qflags, keys);
								} else {
									result = LDNS_STATUS_CRYPTO_NO_TRUSTED_DNSKEY;
								}
							} else {
								result = do_chase(res, ldns_rr_rdf(cur_sig, 7), LDNS_RR_TYPE_DNSKEY, c, trusted_keys, pkt, qflags, keys);
								/* in case key was not self-signed at all, try ds anyway */
								/* TODO: is this needed? clutters the output... */
								/*
								if (result != LDNS_STATUS_OK) {
									result = do_chase(res, ldns_rr_rdf(cur_sig, 7), LDNS_RR_TYPE_DS, c, trusted_keys, pkt, qflags, keys);
								}
								*/
							}
							ldns_rr_list_deep_free(rrset);
							ldns_rr_list_deep_free(sigs);
							ldns_rr_list_deep_free(keys);
							ldns_pkt_free(pkt);
							ldns_rr_free(cur_sig);
							return result;
						}
					/*
					} else {
						printf("Keytag mismatch: %u <> %u\n",
							ldns_calc_keytag(ldns_rr_list_rr(keys, key_i)),
							ldns_rdf2native_int16(ldns_rr_rrsig_keytag(cur_sig))
					   );
					*/	
					}
				}
				if (result != LDNS_STATUS_OK) {
					ldns_rr_list_deep_free(rrset);
					ldns_rr_list_deep_free(sigs);
					ldns_rr_list_deep_free(keys);
					ldns_pkt_free(pkt);
					ldns_rr_free(cur_sig);
					return result;
				}
				ldns_rr_list_deep_free(keys);
			}
		}
			ldns_rr_free(cur_sig);
		}
		ldns_rr_list_deep_free(rrset);
	}

	if (rrset && ldns_rr_list_rr_count(sigs) > 0) {
		ldns_rr_list_deep_free(sigs);
		ldns_pkt_free(pkt);
		return LDNS_STATUS_CRYPTO_NO_TRUSTED_DNSKEY;
	} else {
		ldns_rr_list_deep_free(sigs);
		result = ldns_verify_denial(pkt, name, type, &nsec_rrs, &nsec_rr_sigs);
		if (result == LDNS_STATUS_OK) {
			if (verbosity >= 2) {
				printf(";; Existence denied by nsec(3), chasing nsec record\n");
			}
			/* verify them, they can't be blindly chased */
			result = do_chase(res,
			                  ldns_rr_owner(ldns_rr_list_rr(nsec_rrs, 0)),
			                  ldns_rr_get_type(ldns_rr_list_rr(nsec_rrs, 0)),
			                  c, trusted_keys, pkt, qflags, NULL);
		} else {
			if (verbosity >= 2) {
				printf(";; Denial of existence was not covered: %s\n", ldns_get_errorstr_by_id(result));
			}
		}

		
#if 0
		/* Try to see if there are NSECS in the packet */
		nsecs = ldns_pkt_rr_list_by_type(pkt, LDNS_RR_TYPE_NSEC, LDNS_SECTION_ANY_NOQUESTION);
		result = LDNS_STATUS_CRYPTO_NO_RRSIG;
		
		ldns_rr_list2canonical(nsecs);
		
		for (nsec_i = 0; nsec_i < ldns_rr_list_rr_count(nsecs); nsec_i++) {
			/* there are four options:
			 * - name equals ownername and is covered by the type bitmap
			 * - name equals ownername but is not covered by the type bitmap
			 * - name falls within nsec coverage but is not equal to the owner name
			 * - name falls outside of nsec coverage
			 */
			if (ldns_dname_compare(ldns_rr_owner(ldns_rr_list_rr(nsecs, nsec_i)), name) == 0) {
/*
printf("CHECKING NSEC:\n");
ldns_rr_print(stdout, ldns_rr_list_rr(nsecs, nsec_i));
printf("DAWASEM\n");
*/
				if (ldns_nsec_bitmap_covers_type(ldns_rr_rdf(ldns_rr_list_rr(nsecs, nsec_i), 2), type)) {
					/* Error, according to the nsec this rrset is signed */
					result = LDNS_STATUS_CRYPTO_NO_RRSIG;
				} else {
					/* ok nsec denies existence, chase the nsec now */
					printf(";; Existence of data set with this type denied by NSEC\n");
					result = do_chase(res, ldns_rr_owner(ldns_rr_list_rr(nsecs, nsec_i)), LDNS_RR_TYPE_NSEC, c, trusted_keys, pkt, qflags, NULL);
					if (result == LDNS_STATUS_OK) {
						ldns_pkt_free(pkt);
						printf(";; Verifiably insecure.\n");
						ldns_rr_list_deep_free(nsecs);
						return result;
					}
				}
			} else if (ldns_nsec_covers_name(ldns_rr_list_rr(nsecs, nsec_i), name)) {
				/* Verifably insecure? chase the covering nsec */
				printf(";; Existence of data set with this name denied by NSEC\n");
				result = do_chase(res, ldns_rr_owner(ldns_rr_list_rr(nsecs, nsec_i)), LDNS_RR_TYPE_NSEC, c, trusted_keys, pkt, qflags, NULL);
				if (result == LDNS_STATUS_OK) {
					ldns_pkt_free(pkt);
					printf(";; Verifiably insecure.\n");
					ldns_rr_list_deep_free(nsecs);
					return result;
				}
			} else {
				/* nsec has nothing to do with this data */
			}
		}
		ldns_rr_list_deep_free(nsecs);
		
		nsecs = ldns_pkt_rr_list_by_type(pkt, LDNS_RR_TYPE_NSEC3, LDNS_SECTION_ANY_NOQUESTION);
		nsec_i = 0;
		/* TODO: verify that all nsecs have same iterations and hash values */
		
		if (ldns_rr_list_rr_count(nsecs) != 0) {
			if (verbosity != -1) {
				printf(";; we have nsec3's and no data? prove denial.\n");
				ldns_rr_list_print(stdout, nsecs);
			}

			wildcard_name = ldns_dname_new_frm_str("*");
			chopped_dname = ldns_dname_left_chop(name);
			result = ldns_dname_cat(wildcard_name, chopped_dname);
			ldns_rdf_deep_free(chopped_dname);

			if (ldns_pkt_get_rcode(pkt) == LDNS_RCODE_NXDOMAIN) {
				/* Section 8.4 */
				nsec3_ce = ldns_nsec3_closest_encloser(name, type, nsecs);
				nsec3_wc_ce = ldns_nsec3_closest_encloser(wildcard_name, type, nsecs);				
				if (nsec3_ce && nsec3_wc_ce) {
					printf(";; NAMEERR proven by closest encloser and wildcard encloser NSECS\n");
				} else {
					if (!nsec3_ce) {
						printf(";; NAMEERR oculd not be proven, missing closest encloser\n");
					}
					if (!nsec3_wc_ce) {
						printf(";; NAMEERR oculd not be proven, missing wildcard encloser\n");
					}
				}
				ldns_rdf_deep_free(nsec3_ce);
				ldns_rdf_deep_free(nsec3_wc_ce);
			} else if (ldns_pkt_get_rcode(pkt) == LDNS_RCODE_NOERROR) {
				nsec3_ex = ldns_nsec3_exact_match(name, type, nsecs);
				if (nsec3_ex) {
					nsec3_ce = NULL;
				} else {
					nsec3_ce = ldns_nsec3_closest_encloser(name, type, nsecs);
				}
				nsec3_wc_ex = ldns_nsec3_exact_match(name, type, nsecs);
				if (nsec3_wc_ex) {
					nsec3_wc_ce = NULL;
				} else {
					nsec3_wc_ce = ldns_nsec3_closest_encloser(wildcard_name, type, nsecs);				
				}
				nsec3_wc_ex = ldns_nsec3_exact_match(name, type, nsecs);
				if (!nsec3_wc_ex) {
					if (type != LDNS_RR_TYPE_DS) {
						/* Section 8.5 */
						nsec3_ex = ldns_nsec3_exact_match(name, type, nsecs);
						if (nsec3_ex && !ldns_nsec_bitmap_covers_type(ldns_nsec3_bitmap(nsec3_ex), type)) {
							// ok
							printf(";; NODATA/NOERROR proven for type != DS (draft nsec3-07 section 8.5.)\n");
							printf(";; existence denied\n");
						} else {
							printf(";; NODATA/NOERROR NOT proven for type != DS (draft nsec3-07 section 8.5.)\n");
							printf(";; existence not denied\n");
							result = LDNS_STATUS_ERR;
						}
					} else {
						/* Section 8.6 */
						nsec3_ex = ldns_nsec3_exact_match(name, type, nsecs);
						nsec3_ce = ldns_nsec3_closest_encloser(name, type, nsecs);
						if (!nsec3_ex) {
							nsec3_ce = ldns_nsec3_closest_encloser(name, type, nsecs);
							nsec3_ex = ldns_nsec3_exact_match(nsec3_ce, type, nsecs);
							if (nsec3_ex && ldns_nsec3_optout(nsec3_ex)) {
								printf(";; DS record in optout range of NSEC3 (draft nsec3-07 section 8.6.)");
							} else {
								printf(";; DS record in range of NSEC3 but OPTOUT not set (draft nsec3-07 section 8.6.)\n");
								result = LDNS_STATUS_ERR;
							}
						} else {
							if (nsec3_ex && !ldns_nsec_bitmap_covers_type(ldns_nsec3_bitmap(nsec3_ex), type)) {
								// ok
								printf(";; NODATA/NOERROR proven for type == DS (draft nsec3-07 section 8.6.)\n");
								printf(";; existence denied\n");
							} else {
								printf(";; NODATA/NOERROR NOT proven for type == DS (draft nsec3-07 section 8.6.)\n");
								printf(";; existence not denied\n");
								result = LDNS_STATUS_ERR;
							}
						}
						ldns_rdf_deep_free(nsec3_ce);
					}
				} else {
					if (!ldns_nsec_bitmap_covers_type(ldns_nsec3_bitmap(nsec3_wc_ex), type)) {
						/* Section 8.7 */
						nsec3_ce = ldns_nsec3_closest_encloser(name, type, nsecs);
						if (nsec3_ce) {
							wildcard_name = ldns_dname_new_frm_str("*");
							result = ldns_dname_cat(wildcard_name, nsec3_ce);
							nsec3_wc_ex = ldns_nsec3_exact_match(wildcard_name, type, nsecs);
							if (nsec3_wc_ex) {
								printf(";; Wilcard exists but not for this type (draft nsec3-07 section 8.7.)\n");
							} else {
								printf(";; Error proving wildcard for different type, no proof for wildcard of closest encloser (draft nsec3-07 section 8.7.)\n");
							}
						} else {
							printf(";; NODATA/NOERROR wildcard for other type, error, no closest encloser (draft nsec3-07 section 8.7.)\n");
							result = LDNS_STATUS_ERR;
						}
					} else {
						/* Section 8.8 */
						/* TODO this is not right */
						anc_name = ldns_dname_left_chop(wildcard_name);
						nsec3_wc_ce = ldns_nsec3_closest_encloser(anc_name, type, nsecs);
						if (nsec3_wc_ce) {
							/* must be immediate ancestor */
							if (ldns_dname_compare(anc_name, nsec3_wc_ce) == 0) {
								printf(";; wildcard proven (draft nsec3-07 section 8.8.)\n");
							} else {
								printf(";; closest encloser is not immediate parent of generating wildcard (8.8)\n");
								result = LDNS_STATUS_ERR;
							}
						} else {
							printf(";; Error finding wildcard closest encloser, no proof for wildcard (draft nsec3-07 section 8.8.)\n");
							result = LDNS_STATUS_ERR;
						}
						ldns_rdf_deep_free(anc_name);
						ldns_rdf_deep_free(nsec3_wc_ce);
					}
					/* 8.9 still missing? */
				}

			}
			ldns_rdf_deep_free(wildcard_name);
		}
		
		ldns_rr_list_deep_free(nsecs);
		ldns_pkt_free(pkt);
		printf("Result for ");
		ldns_rdf_print(stdout, name);

		printf(" (");
		if (descriptor && descriptor->_name) {
			printf("%s", descriptor->_name);
		} else {
			printf("TYPE%d\t", 
					type);
		}
		
		printf("): %s\n", ldns_get_errorstr_by_id(result));
#endif
		return result;
	}
}

