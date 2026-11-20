#pragma once
#include "common.h"

#define PSA_KEY_ATTRIBUTES_INIT {0}
#define PSA_KEY_TYPE_ECC_PUBLIC_KEY(family) (family)
#define PSA_ECC_FAMILY_SECP_R1 0x12
#define PSA_KEY_USAGE_VERIFY_MESSAGE 0x800
#define PSA_ALG_ECDSA(hash) ((hash) | 0x06000600)
typedef int psa_algorithm_t;
typedef int mbedtls_svc_key_id_t;
typedef struct {int type,bits,usage,algorithm;} psa_key_attributes_t;
static inline void psa_set_key_type(psa_key_attributes_t *a,int type){a->type=type;}
static inline void psa_set_key_bits(psa_key_attributes_t *a,int bits){a->bits=bits;}
static inline void psa_set_key_usage_flags(psa_key_attributes_t *a,int usage){a->usage=usage;}
static inline void psa_set_key_algorithm(psa_key_attributes_t *a,int algorithm){a->algorithm=algorithm;}
int psa_import_key(const psa_key_attributes_t*,const uint8_t*,size_t,mbedtls_svc_key_id_t*);
int psa_verify_message(mbedtls_svc_key_id_t,psa_algorithm_t,const uint8_t*,size_t,const uint8_t*,size_t);
int psa_destroy_key(mbedtls_svc_key_id_t);
