/*
 * DAG OF BIT-VECTOR EXPRESSIONS
 */

#include <assert.h>

#include "memalloc.h"
#include "bit_tricks.h"
#include "bv64_constants.h"
#include "index_vectors.h"
#include "hash_functions.h"
#include "int_array_sort.h"

#include "bvpoly_dag.h"



/*
 * LIST OPERATIONS
 */

/*
 * Initialize list[k] to a singleton list
 */
static inline void init_list(bvc_item_t *list, int32_t k) {
  list[k].pre = k;
  list[k].next = k;
}


/*
 * Add i before k in list[k]
 */
static inline void list_add(bvc_item_t *list, int32_t k, int32_t i) {
  int32_t j;

  assert(i != k);

  j = list[k].pre;
  list[j].next = i;
  list[i].pre = j;
  list[i].next = k;
  list[k].pre = i;
}


/*
 * Length of list k
 */
static uint32_t list_length(bvc_item_t *list, int32_t k) {
  uint32_t n;
  int32_t j;

  n = 0;
  j = list[k].next;
  while (j != k) {
    n ++;
    j = list[j].next;
  }

  return n;
}


/*
 * Remove i from its current list
 */
static inline void list_remove(bvc_item_t *list, int32_t i) {
  int32_t j, k;

  j = list[i].pre;
  k = list[i].next;
  list[j].next = k;
  list[k].pre = j;
}



/*
 * Add n to one of the three node lists:
 * - list[0]  --> leaves
 * - list[-1] --> elementary nodes
 * - list[-2] --> default list
 */
static inline void bvc_dag_add_to_leaves(bvc_dag_t *dag, bvnode_t n) {
  assert(0 < n && n <= dag->nelems);
  list_add(dag->list, BVC_DAG_LEAF_LIST, n);
}

static inline void bvc_dag_add_to_elementary_list(bvc_dag_t *dag, bvnode_t n) {
  assert(0 < n && n <= dag->nelems);
  list_add(dag->list, BVC_DAG_ELEM_LIST, n);
}

static inline void bvc_dag_add_to_default_list(bvc_dag_t *dag, bvnode_t n) {
  assert(0 < n && n <= dag->nelems);
  list_add(dag->list, BVC_DAG_DEFAULT_LIST, n);
}


/*
 * Move n to a different list
 */
static inline void bvc_dag_move_to_leaves(bvc_dag_t *dag, bvnode_t n) {
  assert(0 < n && n <= dag->nelems);
  list_remove(dag->list, n);
  list_add(dag->list, BVC_DAG_LEAF_LIST, n);
}

static inline void bvc_dag_move_to_elementary_list(bvc_dag_t *dag, bvnode_t n) {
  assert(0 < n && n <= dag->nelems);
  list_remove(dag->list, n);
  list_add(dag->list, BVC_DAG_ELEM_LIST, n);
}


/*
 *  Auxiliary list
 */
void bvc_move_node_to_aux_list(bvc_dag_t *dag, bvnode_t n) {
  assert(0 < n && n <= dag->nelems);
  list_remove(dag->list, n);
  list_add(dag->list, BVC_DAG_AUX_LIST, n);
}

/*
 * Move list with header k into the list with header j
 * list[j] must be empty (contain only j).
 */
static void bvc_move_list(bvc_item_t *list, int32_t k, int32_t j) {
  int32_t next_k, pre_k;

  assert(j != k && j<=0 && k<=0 && list[j].pre == j && list[j].next == j);
  pre_k = list[k].pre;
  next_k = list[k].next;
  if (pre_k != k) {
    assert(next_k != k);
    list[j].pre = pre_k;
    list[pre_k].next = j;
    list[j].next = next_k;
    list[next_k].pre = j;

    list[k].pre = k;
    list[k].next = k;
  }
}

/*
 * Restore the elementary or dafault list from the aux list
 */
void bvc_move_aux_to_elem_list(bvc_dag_t *dag) {
  bvc_move_list(dag->list, BVC_DAG_AUX_LIST, BVC_DAG_ELEM_LIST);
}

void bvc_move_aux_to_complex_list(bvc_dag_t *dag) {
  bvc_move_list(dag->list, BVC_DAG_AUX_LIST, BVC_DAG_DEFAULT_LIST);
}


/*
 * DAG OPERATIONS
 */

/*
 * Initialize dag:
 * - n = initial size. If n=0, use the default size.
 */
void init_bvc_dag(bvc_dag_t *dag, uint32_t n) {
  bvc_item_t *tmp;

  if (n == 0) {
    n = DEF_BVC_DAG_SIZE;
  }
  if (n >= MAX_BVC_DAG_SIZE) {
    out_of_memory();
  }
  assert(n > 0);

  dag->desc = (bvc_header_t **) safe_malloc(n * sizeof(bvc_header_t *));
  dag->use = (int32_t **) safe_malloc(n * sizeof(int32_t *));
  tmp = (bvc_item_t *) safe_malloc((n + 3) * sizeof(bvc_item_t));
  dag->list = tmp + 3;

  dag->desc[0] = NULL;
  dag->use[0] = NULL;
  init_list(dag->list, -3);
  init_list(dag->list, -2);
  init_list(dag->list, -1);
  init_list(dag->list, 0);  

  dag->nelems = 0;
  dag->size = n;

  init_int_htbl(&dag->htbl, 0);
  init_int_bvset(&dag->vset, 0);  // use bvset default size (1024)
  init_int_hmap(&dag->vmap, 128);

  init_objstore(&dag->leaf_store, sizeof(bvc_leaf_t), 500);
  init_objstore(&dag->offset_store, sizeof(bvc_offset_t), 500);
  init_objstore(&dag->mono_store, sizeof(bvc_mono_t), 500);
  init_objstore(&dag->prod_store, sizeof(bvc_prod_t) + PROD_STORE_LEN * sizeof(varexp_t), 100);
  init_objstore(&dag->sum_store1, sizeof(bvc_sum_t) + SUM_STORE1_LEN * sizeof(int32_t), 500);
  init_objstore(&dag->sum_store2, sizeof(bvc_sum_t) + SUM_STORE2_LEN * sizeof(int32_t), 500);
  init_objstore(&dag->alias_store, sizeof(bvc_alias_t), 100);

  init_bvconstant(&dag->aux);
  init_pp_buffer(&dag->pp_aux, 10);
  init_ivector(&dag->buffer, 10);
}



/*
 * Increase the size (by 50%)
 */
static void extend_bvc_dag(bvc_dag_t *dag) {
  bvc_item_t *tmp;
  uint32_t n;

  n = dag->size + 1;
  n += (n >> 1);
  if (n >= MAX_BVC_DAG_SIZE) {
    out_of_memory();
  }

  assert(n > dag->size);

  dag->desc = (bvc_header_t **) safe_realloc(dag->desc, n * sizeof(bvc_header_t *));
  dag->use = (int32_t **) safe_realloc(dag->use, n * sizeof(int32_t *));
  tmp = dag->list - 3;
  tmp = (bvc_item_t *) safe_realloc(tmp, (n + 3) * sizeof(bvc_item_t));
  dag->list = tmp + 3;

  dag->size = n;
}


/*
 * Add a new node n with descriptor d
 * - set use[n] to NULL
 * - list[n] is not initialized
 */
static bvnode_t bvc_dag_add_node(bvc_dag_t *dag, bvc_header_t *d) {
  uint32_t i;

  i = dag->nelems + 1;
  if (i == dag->size) {
    extend_bvc_dag(dag);
  }
  assert(i < dag->size);

  dag->desc[i] = d;
  dag->use[i] = NULL;

  dag->nelems = i;

  return i;
}


/*
 * Free memory used by descriptor d
 * - free d itself if it's not form a store (i.e., d->size is large)
 * - free d->constant.w if d->bitsize > 64
 */
static void delete_descriptor(bvc_header_t *d) {
  switch (d->tag) {
  case BVC_LEAF:
    break;

  case BVC_OFFSET:
    if (d->bitsize > 64) {
      bvconst_free(offset_node(d)->constant.w, (d->bitsize + 31) >> 5);
    }
    break;

  case BVC_MONO:
    if (d->bitsize > 64) {
      bvconst_free(mono_node(d)->coeff.w, (d->bitsize + 31) >> 5);
    }
    break;

  case BVC_PROD:
    if (prod_node(d)->size > PROD_STORE_LEN) {
      safe_free(d);
    }
    break;

  case BVC_SUM:
    if (sum_node(d)->size > SUM_STORE2_LEN) {
      safe_free(d);
    }
    break;

  case BVC_ALIAS:
    break;
  }
}


/*
 * Delete the DAG
 */
void delete_bvc_dag(bvc_dag_t *dag) {
  uint32_t i, n;

  n = dag->nelems;
  for (i=1; i<=n; i++) {
    delete_descriptor(dag->desc[i]);
    delete_index_vector(dag->use[i]);
  }

  safe_free(dag->desc);
  safe_free(dag->use);
  safe_free(dag->list - 3);

  dag->desc = NULL;
  dag->use = NULL;
  dag->list = NULL;

  delete_int_htbl(&dag->htbl);
  delete_int_bvset(&dag->vset);
  delete_int_hmap(&dag->vmap);

  delete_objstore(&dag->leaf_store);
  delete_objstore(&dag->offset_store);
  delete_objstore(&dag->mono_store);
  delete_objstore(&dag->prod_store);
  delete_objstore(&dag->sum_store1);
  delete_objstore(&dag->sum_store2);
  delete_objstore(&dag->alias_store);

  delete_bvconstant(&dag->aux);
  delete_pp_buffer(&dag->pp_aux);
  delete_ivector(&dag->buffer);
}


/*
 * Empty: remove all nodes
 */
void reset_bvc_dag(bvc_dag_t *dag) {
  uint32_t i, n;

  n = dag->nelems;
  for (i=1; i<=n; i++) {
    delete_descriptor(dag->desc[i]);
    delete_index_vector(dag->use[i]);
  }

  dag->nelems = 0;

  // reset the lists
  init_list(dag->list, -3);
  init_list(dag->list, -2);
  init_list(dag->list, -1);
  init_list(dag->list, 0);  

  reset_int_htbl(&dag->htbl);
  reset_int_bvset(&dag->vset);
  int_hmap_reset(&dag->vmap);

  reset_objstore(&dag->leaf_store);
  reset_objstore(&dag->offset_store);
  reset_objstore(&dag->mono_store);
  reset_objstore(&dag->prod_store);
  reset_objstore(&dag->sum_store1);
  reset_objstore(&dag->sum_store2);
  reset_objstore(&dag->alias_store);

  pp_buffer_reset(&dag->pp_aux);
  ivector_reset(&dag->buffer);
}






/*
 * NODE DESCRIPTOR ALLOCATION
 */

/*
 * Descriptor allocation
 * - for prod and sum, n = length of the sum or product
 */
static inline bvc_leaf_t *alloc_leaf(bvc_dag_t *dag) {
  return (bvc_leaf_t *) objstore_alloc(&dag->leaf_store);
}

static inline bvc_offset_t *alloc_offset(bvc_dag_t *dag) {
  return (bvc_offset_t *) objstore_alloc(&dag->offset_store);
}

static inline bvc_mono_t *alloc_mono(bvc_dag_t *dag) {
  return (bvc_mono_t *) objstore_alloc(&dag->mono_store);
}

static bvc_prod_t *alloc_prod(bvc_dag_t *dag, uint32_t n) {
  void *tmp;

  if (n <= PROD_STORE_LEN) {
    tmp = objstore_alloc(&dag->prod_store);
  } else if (n <= MAX_BVC_PROD_LEN) {
    tmp = safe_malloc(sizeof(bvc_prod_t) + n * sizeof(varexp_t));
  } else {
    out_of_memory();
  }

  return (bvc_prod_t *) tmp;
}

static bvc_sum_t *alloc_sum(bvc_dag_t *dag, uint32_t n) {
  void *tmp;

  if (n <= SUM_STORE1_LEN) {
    tmp = objstore_alloc(&dag->sum_store1);
  } else if (n <= SUM_STORE2_LEN) {
    tmp = objstore_alloc(&dag->sum_store2);
  } else if (n <= MAX_BVC_SUM_LEN) {
    tmp = safe_malloc(sizeof(bvc_sum_t) + n * sizeof(int32_t));
  } else {
    out_of_memory();
  }

  return (bvc_sum_t *) tmp;
}


static inline bvc_alias_t *alloc_alias(bvc_dag_t *dag) {
  return (bvc_alias_t *) objstore_alloc(&dag->alias_store);
}


/*
 * De-allocation
 */
static inline void free_leaf(bvc_dag_t *dag, bvc_leaf_t *d) {
  objstore_free(&dag->leaf_store, d);
}

static void free_offset(bvc_dag_t *dag, bvc_offset_t *d) {
  if (d->header.bitsize > 64) {
    bvconst_free(d->constant.w, (d->header.bitsize + 31) >> 5);
  }
  objstore_free(&dag->offset_store, d);
}

static void free_mono(bvc_dag_t *dag, bvc_mono_t *d) {
  if (d->header.bitsize > 64) {
    bvconst_free(d->coeff.w, (d->header.bitsize + 31) >> 5);
  }
  objstore_free(&dag->mono_store, d);
}

static void free_prod(bvc_dag_t *dag, bvc_prod_t *d) {
  if (d->size <= PROD_STORE_LEN) {
    objstore_free(&dag->prod_store, d);
  } else {
    safe_free(d);
  }
}

static void free_sum(bvc_dag_t *dag, bvc_sum_t *d) {
  if (d->size <= SUM_STORE1_LEN) {
    objstore_free(&dag->sum_store1, d);
  } else if (d->size <= SUM_STORE2_LEN) {
    objstore_free(&dag->sum_store2, d);
  } else {
    safe_free(d);
  }
}

static inline void free_alias(bvc_dag_t *dag, bvc_alias_t *d) {
  objstore_free(&dag->alias_store, d);
}

static void free_descriptor(bvc_dag_t *dag, bvc_header_t *d) {
  switch (d->tag) {
  case BVC_LEAF:
    free_leaf(dag, leaf_node(d));
    break;

  case BVC_OFFSET:
    free_offset(dag, offset_node(d));
    break;

  case BVC_MONO:
    free_mono(dag, mono_node(d));
    break;

  case BVC_PROD:
    free_prod(dag, prod_node(d));
    break;

  case BVC_SUM:
    free_sum(dag, sum_node(d));
    break;

  case BVC_ALIAS:
    free_alias(dag, alias_node(d));
    break;
  }
}



/*
 * Check whether a node is elementary
 */
static inline bool offset_node_is_elementary(bvc_dag_t *dag, bvc_offset_t *d) {
  return bvc_dag_occ_is_leaf(dag, d->nocc);
}

static inline bool mono_node_is_elementary(bvc_dag_t *dag, bvc_mono_t *d) {
  return bvc_dag_occ_is_leaf(dag, d->nocc);
}

static bool prod_node_is_elementary(bvc_dag_t *dag, bvc_prod_t *d) {
  assert(d->len >= 1);

  if (d->len == 1) {
    return d->prod[0].exp == 2 && bvc_dag_occ_is_leaf(dag, d->prod[0].var);
  } else if (d->len == 2) {
    return d->prod[0].exp + d->prod[1].exp == 2 &&
      bvc_dag_occ_is_leaf(dag, d->prod[0].var) &&
      bvc_dag_occ_is_leaf(dag, d->prod[1].var);
  } else {
    return false;
  }    
}

static bool sum_node_is_elementary(bvc_dag_t *dag, bvc_sum_t * d) {
  assert(d->len >= 2);
  return d->len == 2 && bvc_dag_occ_is_leaf(dag, d->sum[0]) && bvc_dag_occ_is_leaf(dag, d->sum[1]);
}


static bool node_is_elementary(bvc_dag_t *dag, bvnode_t i) {
  bvc_header_t *d;

  assert(0 < i && i <= dag->nelems);

  d = dag->desc[i];
  switch (d->tag) {
  case BVC_LEAF:
  case BVC_ALIAS:
    break;

  case BVC_OFFSET:
    return offset_node_is_elementary(dag, offset_node(d));

  case BVC_MONO:
    return mono_node_is_elementary(dag, mono_node(d));

  case BVC_PROD:
    return prod_node_is_elementary(dag, prod_node(d));

  case BVC_SUM:
    return sum_node_is_elementary(dag, sum_node(d));
  }

  return false;
}




/*
 * MORE CHECKS
 */

uint32_t bvnode_num_occs(bvc_dag_t *dag, bvnode_t i) {
  int32_t *l;

  assert(0 < i && i <= dag->nelems);
  l = dag->use[i];
  return l != NULL ? iv_size(l) : 0;
}


/*
 * Check whether n is shared (i.e., it occurs more than once)
 */
bool bvc_dag_occ_is_shared(bvc_dag_t *dag, node_occ_t n) {
  int32_t *l;

  assert(0 < node_of_occ(n) && node_of_occ(n) <= dag->nelems);

  l = dag->use[node_of_occ(n)];
  return l != NULL && iv_size(l) > 1;
}





/*
 * NODE CONSTRUCTION
 */

/*
 * Add i to the use list of n
 */
static inline void bvc_dag_add_dependency(bvc_dag_t *dag, bvnode_t n, bvnode_t i) {
  assert(0 < n && n <= dag->nelems && 0 < i && i <= dag->nelems && i != n);
  add_index_to_vector(dag->use + n, i);
}


/*
 * Bit hash: 
 * - for a node index n, the bit_hash is a 32bit word
 *   equal to (1 << (n & 31)): i.e., bit i is set if (n % 32 == i).
 * - for a set of node indices, the bit hash is the bitwise or
 *   of the bit_hash of each element
 *
 * This gives a quick filter to test inclusion between sets of 
 * nodes: if bit_hash(A) & bit_hash(B) != bit_hash(A) then
 * A can't be a subset of B.
 */
static inline uint32_t bit_hash(bvnode_t n) {
  assert(n > 0);
  return ((uint32_t) 1) << (n & 31);
}

static inline uint32_t bit_hash_occ(node_occ_t n) {
  return bit_hash(node_of_occ(n));
}


/*
 * Create a leaf node
 */
static bvnode_t bvc_dag_mk_leaf(bvc_dag_t *dag, int32_t x, uint32_t bitsize) {
  bvc_leaf_t *d;
  bvnode_t q;

  d = alloc_leaf(dag);
  d->header.tag = BVC_LEAF;
  d->header.bitsize = bitsize;
  d->map = x;

  q = bvc_dag_add_node(dag, &d->header);
  bvc_dag_add_to_leaves(dag, q);

  return q;
}


/*
 * Create an offset node q := [offset a n]
 */
static bvnode_t bvc_dag_mk_offset64(bvc_dag_t *dag, uint64_t a, node_occ_t n, uint32_t bitsize) {
  bvc_offset_t *d;
  bvnode_t q;
  int32_t k;

  assert(1 <= bitsize && bitsize <= 64 && a == norm64(a, bitsize));

  d = alloc_offset(dag);
  d->header.tag = BVC_OFFSET;
  d->header.bitsize = bitsize;
  d->nocc = n;
  d->constant.c = a;

  q = bvc_dag_add_node(dag, &d->header);
  bvc_dag_add_dependency(dag, node_of_occ(n), q); // q depends on n

  k = BVC_DAG_DEFAULT_LIST;
  if (bvc_dag_occ_is_leaf(dag, n)) {
    k = BVC_DAG_ELEM_LIST;
  }
  list_add(dag->list, k, q);

  return q;
}


static bvnode_t bvc_dag_mk_offset(bvc_dag_t *dag, uint32_t *a, node_occ_t n, uint32_t bitsize) {
  bvc_offset_t *d;
  uint32_t *c;
  uint32_t k;
  bvnode_t q;
  int32_t l;

  assert(bitsize > 64);

  // make a copy of a: a must be normalized so the copy will be normalized too
  k = (bitsize + 31) >> 5;
  c = bvconst_alloc(k);
  bvconst_set(c, k, a);
  assert(bvconst_is_normalized(c, bitsize));

  d = alloc_offset(dag);
  d->header.tag = BVC_OFFSET;
  d->header.bitsize = bitsize;
  d->nocc = n;
  d->constant.w = c;

  q = bvc_dag_add_node(dag, &d->header);
  bvc_dag_add_dependency(dag, node_of_occ(n), q); // q depends on n

  l = BVC_DAG_DEFAULT_LIST;
  if (bvc_dag_occ_is_leaf(dag, n)) {
    l = BVC_DAG_ELEM_LIST;
  }
  list_add(dag->list, l, q);


  return q;
}



/*
 * Create an monomial node q := [mono a, n]
 */
static bvnode_t bvc_dag_mk_mono64(bvc_dag_t *dag, uint64_t a, node_occ_t n, uint32_t bitsize) {
  bvc_mono_t *d;
  bvnode_t q;
  int32_t k;

  assert(1 <= bitsize && bitsize <= 64 && a == norm64(a, bitsize));

  d = alloc_mono(dag);
  d->header.tag = BVC_MONO;
  d->header.bitsize = bitsize;
  d->nocc = n;
  d->coeff.c = a;

  q = bvc_dag_add_node(dag, &d->header);
  bvc_dag_add_dependency(dag, node_of_occ(n), q); // q depends on n

  k = BVC_DAG_DEFAULT_LIST;
  if (bvc_dag_occ_is_leaf(dag, n)) {
    k = BVC_DAG_ELEM_LIST;
  }
  list_add(dag->list, k, q);


  return q;
}


static bvnode_t bvc_dag_mk_mono(bvc_dag_t *dag, uint32_t *a, node_occ_t n, uint32_t bitsize) {
  bvc_mono_t *d;
  uint32_t *c;
  uint32_t k;
  bvnode_t q;
  int32_t l;

  assert(bitsize > 64 && bvconst_is_normalized(a, bitsize));

  // make a copy of a.
  // a must be normalized so the copy will be normalized too
  k = (bitsize + 31) >> 5;
  c = bvconst_alloc(k);
  bvconst_set(c, k, a);
  assert(bvconst_is_normalized(c, bitsize));

  d = alloc_mono(dag);
  d->header.tag = BVC_MONO;
  d->header.bitsize = bitsize;
  d->nocc = n;
  d->coeff.w = c;

  q = bvc_dag_add_node(dag, &d->header);
  bvc_dag_add_dependency(dag, node_of_occ(n), q); // q depends on n

  l = BVC_DAG_DEFAULT_LIST;
  if (bvc_dag_occ_is_leaf(dag, n)) {
    l = BVC_DAG_ELEM_LIST;
  }
  list_add(dag->list, l, q);

  return q;
}




/*
 * Product node defined by a[0 ... n-1]:
 * - each a[i] is a pair (node, exponent)
 */
static bvnode_t bvc_dag_mk_prod(bvc_dag_t *dag, varexp_t *a, uint32_t n, uint32_t bitsize) {
  bvc_prod_t *d;
  uint32_t i;
  int32_t q, k;

  d = alloc_prod(dag, n);
  d->header.tag = BVC_PROD;
  d->header.bitsize = bitsize;
  d->hash = 0;
  d->size = n;
  d->len = n;
  for (i=0; i<n; i++) {
    d->prod[i] = a[i];
    d->hash |= bit_hash_occ(a[i].var);
  }

  q = bvc_dag_add_node(dag, &d->header);
  for (i=0; i<n; i++) {
    bvc_dag_add_dependency(dag, node_of_occ(a[i].var), q);
  }
  
  k = BVC_DAG_DEFAULT_LIST;
  if (prod_node_is_elementary(dag, d)) {
    k = BVC_DAG_ELEM_LIST;
  }
  list_add(dag->list, k, q);

  return q;
}



/*
 * Sum mode a[0] + ... + a[n-1]
 * - each a[i] is a node occurrence
 */
static bvnode_t bvc_dag_mk_sum(bvc_dag_t *dag, node_occ_t *a, uint32_t n, uint32_t bitsize) {
  bvc_sum_t *d;
  uint32_t i;
  bvnode_t q;
  int32_t k;

  d = alloc_sum(dag, n);
  d->header.tag = BVC_SUM;
  d->header.bitsize = bitsize;
  d->hash = 0;
  d->size = n;
  d->len = n;
  for (i=0; i<n; i++) {
    d->sum[i] = a[i];
    d->hash |= bit_hash_occ(a[i]);
  }

  q = bvc_dag_add_node(dag, &d->header);
  for (i=0; i<n; i++) {
    bvc_dag_add_dependency(dag, node_of_occ(a[i]), q);
  }

  k = BVC_DAG_DEFAULT_LIST;
  if (sum_node_is_elementary(dag, d)) {
    k = BVC_DAG_ELEM_LIST;
  }
  list_add(dag->list, k, q);

  return q;
}


/*
 * HASH CONSING
 */

typedef struct bvc_leaf_hobj_s {
  int_hobj_t m;
  bvc_dag_t *dag;
  uint32_t bitsize;
  int32_t map;
} bvc_leaf_hobj_t;

// same struct for both offset/mono with 64bit constant
typedef struct bvc64_hobj_s {
  int_hobj_t m;
  bvc_dag_t *dag;
  uint64_t c;
  uint32_t bitsize;
  node_occ_t nocc;
} bvc64_hobj_t;

// struct for offset/mono with larger constant
typedef struct bvc_hobj_s {
  int_hobj_t m;
  bvc_dag_t *dag;
  uint32_t *c;
  uint32_t bitsize;
  node_occ_t nocc;
} bvc_hobj_t;

typedef struct bvc_prod_hobj_s {
  int_hobj_t m;
  bvc_dag_t *dag;
  varexp_t *pp;  
  uint32_t bitsize;
  uint32_t len;
} bvc_prod_hobj_t;

typedef struct bvc_sum_hobj_s {
  int_hobj_t m;
  bvc_dag_t *dag;
  node_occ_t *noccs;
  uint32_t bitsize;
  uint32_t len;
} bvc_sum_hobj_t;


/*
 * Hash functions
 */
static uint32_t hash_bvc_leaf_hobj(bvc_leaf_hobj_t *p) {
  return jenkins_hash_pair(p->map, 0, 0x12930a32);
}

static uint32_t hash_bvc_offset64_hobj(bvc64_hobj_t *p) {
  uint32_t a, b;

  a = jenkins_hash_uint64(p->c);
  b = jenkins_hash_int32(p->nocc);
  return jenkins_hash_pair(a, b, 0x23da32aa);
}

static uint32_t hash_bvc_offset_hobj(bvc_hobj_t *p) {
  uint32_t a, b;

  a = bvconst_hash(p->c, p->bitsize);
  b = jenkins_hash_int32(p->nocc);
  return jenkins_hash_pair(a, b, 0x32288cc9);
}

static uint32_t hash_bvc_mono64_hobj(bvc64_hobj_t *p) {
  uint32_t a, b;

  a = jenkins_hash_uint64(p->c);
  b = jenkins_hash_int32(p->nocc);
  return jenkins_hash_pair(a, b, 0xaef43e27);
}

static uint32_t hash_bvc_mono_hobj(bvc_hobj_t *p) {
  uint32_t a, b;

  a = bvconst_hash(p->c, p->bitsize);
  b = jenkins_hash_int32(p->nocc);
  return jenkins_hash_pair(a, b, 0xfe43a091);
}

// p->pp = array of len pairs of int32_t
static uint32_t hash_bvc_prod_hobj(bvc_prod_hobj_t *p) {
  assert(p->len <= UINT32_MAX/2);
  return jenkins_hash_intarray2((int32_t *) p->pp, 2 * p->len, 0x7432cde2);
}

static uint32_t hash_bvc_sum_hobj(bvc_sum_hobj_t *p) {
  return jenkins_hash_intarray2(p->noccs, p->len, 0xaeb32a06);
}


/*
 * Equality tests
 */
static bool eq_bvc_leaf_hobj(bvc_leaf_hobj_t *p, bvnode_t i) {
  bvc_header_t *d;

  d = p->dag->desc[i];
  return d->tag == BVC_LEAF && leaf_node(d)->map == p->map;
}

static bool eq_bvc_offset64_hobj(bvc64_hobj_t *p, bvnode_t i) {
  bvc_header_t *d;
  bvc_offset_t *o;

  d = p->dag->desc[i];
  if (d->tag != BVC_OFFSET || d->bitsize != p->bitsize) {
    return false;
  }
  o = offset_node(d);
  return o->nocc == p->nocc && o->constant.c == p->c;
}

static bool eq_bvc_offset_hobj(bvc_hobj_t *p, bvnode_t i) {
  bvc_header_t *d;
  bvc_offset_t *o;
  uint32_t k;

  d = p->dag->desc[i];
  if (d->tag != BVC_OFFSET && d->bitsize != p->bitsize) {
    return false;
  }
  o = offset_node(d);
  k = (d->bitsize + 31) >> 5;
  return o->nocc == p->nocc && bvconst_eq(o->constant.w, p->c, k);
}

static bool eq_bvc_mono64_hobj(bvc64_hobj_t *p, bvnode_t i) {
  bvc_header_t *d;
  bvc_mono_t *o;

  d = p->dag->desc[i];
  if (d->tag != BVC_MONO && d->bitsize != p->bitsize) {
    return false;
  } 
  o = mono_node(d);
  return o->nocc == p->nocc && o->coeff.c == p->c;
}

static bool eq_bvc_mono_hobj(bvc_hobj_t *p, bvnode_t i) {
  bvc_header_t *d;
  bvc_mono_t *o;
  uint32_t k;

  d = p->dag->desc[i];
  if (d->tag != BVC_MONO || d->bitsize != p->bitsize) {
    return false;
  }
  o = mono_node(d);
  k = (d->bitsize + 31) >> 5;
  return o->nocc == p->nocc && bvconst_eq(o->coeff.w, p->c, k);
}

static bool eq_bvc_prod_hobj(bvc_prod_hobj_t *p, bvnode_t i) {
  bvc_header_t *d;
  bvc_prod_t *o;
  uint32_t j, n;

  d = p->dag->desc[i];
  if (d->tag != BVC_PROD || d->bitsize != p->bitsize) {
    return false;
  }
  o = prod_node(d);
  n = o->len;
  if (n != p-> len) {
    return false;
  }

  for (j=0; j<n; j++) {
    if (p->pp[j].var != o->prod[j].var ||
	p->pp[j].exp != o->prod[j].exp) {
      return false;
    }
  }

  return true;
}

static bool eq_bvc_sum_hobj(bvc_sum_hobj_t *p, bvnode_t i) {
  bvc_header_t *d;
  bvc_sum_t *o;
  uint32_t j, n;

  d = p->dag->desc[i];
  if (d->tag != BVC_SUM || d->bitsize != p->bitsize) {
    return false;
  }
  o = sum_node(d);
  n = o->len;
  if (n != p-> len) {
    return false;
  }

  for (j=0; j<n; j++) {
    if (p->noccs[j] != o->sum[j]) {
      return false;
    }
  }

  return true;
}


/*
 * Constructors
 */
static bvnode_t build_bvc_leaf_hobj(bvc_leaf_hobj_t *p) {
  return bvc_dag_mk_leaf(p->dag, p->map, p->bitsize);
}

static bvnode_t build_bvc_offset64_hobj(bvc64_hobj_t *p) {
  return bvc_dag_mk_offset64(p->dag, p->c, p->nocc, p->bitsize);
}

static bvnode_t build_bvc_offset_hobj(bvc_hobj_t *p) {
  return bvc_dag_mk_offset(p->dag, p->c, p->nocc, p->bitsize);
}

static bvnode_t build_bvc_mono64_hobj(bvc64_hobj_t *p) {
  return bvc_dag_mk_mono64(p->dag, p->c, p->nocc, p->bitsize);
}

static bvnode_t build_bvc_mono_hobj(bvc_hobj_t *p) {
  return bvc_dag_mk_mono(p->dag, p->c, p->nocc, p->bitsize);
}

static bvnode_t build_bvc_prod_hobj(bvc_prod_hobj_t *p) {
  return bvc_dag_mk_prod(p->dag, p->pp, p->len, p->bitsize);
}

static bvnode_t build_bvc_sum_hobj(bvc_sum_hobj_t *p) {
  return bvc_dag_mk_sum(p->dag, p->noccs, p->len, p->bitsize);
}


/*
 * Hash-consing objects
 */
static bvc_leaf_hobj_t bvc_leaf_hobj = {
  { (hobj_hash_t) hash_bvc_leaf_hobj, (hobj_eq_t) eq_bvc_leaf_hobj, 
    (hobj_build_t) build_bvc_leaf_hobj },
  NULL, 0, 0
};

static bvc64_hobj_t bvc_offset64_hobj = {
  { (hobj_hash_t) hash_bvc_offset64_hobj, (hobj_eq_t) eq_bvc_offset64_hobj, 
    (hobj_build_t) build_bvc_offset64_hobj },
  NULL, 0, 0, 0
};

static bvc_hobj_t bvc_offset_hobj = {
  { (hobj_hash_t) hash_bvc_offset_hobj, (hobj_eq_t) eq_bvc_offset_hobj, 
    (hobj_build_t) build_bvc_offset_hobj },
  NULL, NULL, 0, 0  
};

static bvc64_hobj_t bvc_mono64_hobj = {
  { (hobj_hash_t) hash_bvc_mono64_hobj, (hobj_eq_t) eq_bvc_mono64_hobj, 
    (hobj_build_t) build_bvc_mono64_hobj },
  NULL, 0, 0, 0  
};

static bvc_hobj_t bvc_mono_hobj = {
  { (hobj_hash_t) hash_bvc_mono_hobj, (hobj_eq_t) eq_bvc_mono_hobj, 
    (hobj_build_t) build_bvc_mono_hobj },
  NULL, NULL, 0, 0  
};

static bvc_prod_hobj_t bvc_prod_hobj = {
  { (hobj_hash_t) hash_bvc_prod_hobj, (hobj_eq_t) eq_bvc_prod_hobj, 
    (hobj_build_t) build_bvc_prod_hobj },
  NULL, NULL, 0, 0,
};

static bvc_sum_hobj_t bvc_sum_hobj = {
  { (hobj_hash_t) hash_bvc_sum_hobj, (hobj_eq_t) eq_bvc_sum_hobj, 
    (hobj_build_t) build_bvc_sum_hobj },
  NULL, NULL, 0, 0,
};


/*
 * Hash-consing constructors
 */
static bvnode_t bvc_dag_get_leaf(bvc_dag_t *dag, int32_t x, uint32_t bitsize) {
  bvc_leaf_hobj.dag = dag;
  bvc_leaf_hobj.bitsize = bitsize;
  bvc_leaf_hobj.map = x;
  return int_htbl_get_obj(&dag->htbl, &bvc_leaf_hobj.m);
}

static bvnode_t bvc_dag_get_offset64(bvc_dag_t *dag, uint64_t a, node_occ_t n, uint32_t bitsize) {
  bvc_offset64_hobj.dag = dag;
  bvc_offset64_hobj.c = a;
  bvc_offset64_hobj.bitsize = bitsize;
  bvc_offset64_hobj.nocc = n;
  return int_htbl_get_obj(&dag->htbl, &bvc_offset64_hobj.m);
}

static bvnode_t bvc_dag_get_offset(bvc_dag_t *dag, uint32_t *a, node_occ_t n, uint32_t bitsize) {
  bvc_offset_hobj.dag = dag;
  bvc_offset_hobj.c = a;
  bvc_offset_hobj.bitsize = bitsize;
  bvc_offset_hobj.nocc = n;
  return int_htbl_get_obj(&dag->htbl, &bvc_offset_hobj.m);
}

static bvnode_t bvc_dag_get_mono64(bvc_dag_t *dag, uint64_t a, node_occ_t n, uint32_t bitsize) {
  bvc_mono64_hobj.dag = dag;
  bvc_mono64_hobj.c = a;
  bvc_mono64_hobj.bitsize = bitsize;
  bvc_mono64_hobj.nocc = n;
  return int_htbl_get_obj(&dag->htbl, &bvc_mono64_hobj.m);
}

static bvnode_t bvc_dag_get_mono(bvc_dag_t *dag, uint32_t *a, node_occ_t n, uint32_t bitsize) {
  bvc_mono_hobj.dag = dag;
  bvc_mono_hobj.c = a;
  bvc_mono_hobj.bitsize = bitsize;
  bvc_mono_hobj.nocc = n;
  return int_htbl_get_obj(&dag->htbl, &bvc_mono_hobj.m);
}

// note: a must be sorted
static bvnode_t bvc_dag_get_prod(bvc_dag_t *dag, varexp_t *a, uint32_t len, uint32_t bitsize) {
  bvc_prod_hobj.dag = dag;
  bvc_prod_hobj.pp = a;
  bvc_prod_hobj.bitsize = bitsize;
  bvc_prod_hobj.len = len;
  return int_htbl_get_obj(&dag->htbl, &bvc_prod_hobj.m);
}

// a must be sorted
static bvnode_t bvc_dag_get_sum(bvc_dag_t *dag, node_occ_t *a, uint32_t len, uint32_t bitsize) {
  bvc_sum_hobj.dag = dag;
  bvc_sum_hobj.noccs = a;
  bvc_sum_hobj.bitsize = bitsize;
  bvc_sum_hobj.len = len;
  return int_htbl_get_obj(&dag->htbl, &bvc_sum_hobj.m);
}






/*
 * NORMALIZATION + NODE CONSTRUCTION
 */

/*
 * Store mapping [x --> n] in dag->vmap
 * - x must be positive
 * - n must be a valid node_occurrence in dag
 */
void bvc_dag_map_var(bvc_dag_t *dag, int32_t x, node_occ_t n) {
  int_hmap_pair_t *p;

  assert(x > 0 && !bvc_dag_var_is_present(dag, x));
  int_bvset_add(&dag->vset, x);
  p = int_hmap_get(&dag->vmap, x);
  assert(p->val == -1);
  p->val = n;
}



/*
 * Leaf node attached to variable x
 * - x must be positive
 */
node_occ_t bvc_dag_leaf(bvc_dag_t *dag, int32_t x, uint32_t bitsize) {
  assert(x > 0);
  return  bvp(bvc_dag_get_leaf(dag, x, bitsize));
}


/*
 * Get a node mapped to x
 * - if there's none, create the node [leaf x] and store [x --> [leaf x]]
 *   in the vmap
 */
node_occ_t bvc_dag_get_nocc_of_var(bvc_dag_t *dag, int32_t x, uint32_t bitsize) {
  node_occ_t n;

  assert(x > 0);

  if (bvc_dag_var_is_present(dag, x)) {
    return bvc_dag_nocc_of_var(dag, x);
  } else {
    n = bvc_dag_leaf(dag, x, bitsize);
    bvc_dag_map_var(dag, x, n);
    return n;
  }
}


/*
 * Construct an offset node q
 * - a must be normalized modulo 2^bitsize (and not be 0)
 */
node_occ_t bvc_dag_offset64(bvc_dag_t *dag, uint64_t a, node_occ_t n, uint32_t bitsize) {
  assert(1 <= bitsize && bitsize <= 64 && a == norm64(a, bitsize) && a != 0);
  return bvp(bvc_dag_get_offset64(dag, a, n, bitsize));
}

node_occ_t bvc_dag_offset(bvc_dag_t *dag, uint32_t *a, node_occ_t n, uint32_t bitsize) {
  assert(64 < bitsize && bvconst_is_normalized(a, bitsize));
  return bvp(bvc_dag_get_offset(dag, a, n, bitsize));
}




/*
 * Construct a monomial node q
 * - a must be normalized modulo 2^bitsize and must not be 0
 *
 * Depending on a and n, this gets turned into one of the following nodes:
 * - if a is +1  -->   n
 * - if a is -1  -->  -n
 * - otherwise,
 *   1) force n to positive sign
 *   2) depending on the number of '1' bits in a and -a,
 *      build either [mono a n] or [mono (-a) n]
 *
 * Heuristics:
 * - the number of adders required for (a * n) is equal to the number of '1'
 *   bits in a (i.e., to popcount(a)).
 * - (BVMUL a n) has cost equal to popcount(a)
 *   (BVNEG (BVMUL -a n)) has cost equal to  popcount(-a) + 1 (we count
 *    BVNEG as one more adder)
 *
 *
 * NOTE: there are better techniques
 * - could use a signed digit representation for the constant a
 * - if there are several monomials (a_0 x) ... (a_t x), then
 *   there are optimizations used in digital filter circuits:
 * 
 * Reference: 
 *  Dempster & McLeod, Constant integer multiplication using minimum adders, 
 *  IEE Proceedings, Cicuits, Devices & Systems, vol. 141, Issue 5, pp. 407-413,
 *  October 1994
 */
node_occ_t bvc_dag_mono64(bvc_dag_t *dag, uint64_t a, node_occ_t n, uint32_t bitsize) {
  uint64_t minus_a;
  uint32_t sign, ka, kma;
  bvnode_t q;

  assert(1 <= bitsize && bitsize <= 64 && a == norm64(a, bitsize) && a != 0);

  if (a == 1) return n;
  if (a == mask64(bitsize)) return negate_occ(n);
    
  sign = sign_of_occ(n);
  n = unsigned_occ(n);

  /*
   * normalization: 
   * - is popcount(a)  < popcount(-a) then build [mono a n]
   * - if popcount(-a) < popcount(a)  then build [mono (-a) n]
   * - if there's a tie, we build [mono (-a) n] if -a is positive
   *                           or [mono a n] otherwise
   *
   * Note: if a is 0b10000...00 then both a and -a are negative and equal
   * so the tie-breaking rule works too (we want to build [mono a n]
   * in this case).
   */
  minus_a = norm64(-a, bitsize);
  ka = popcount64(a);
  kma = popcount64(minus_a);
  assert(1 <= ka && ka <= bitsize && 1 <= kma && kma <= bitsize);

  if (kma < ka || (kma == ka && is_pos64(minus_a, bitsize))) {
    a = minus_a;
    sign ^= 1; // flip the sign
  }

  q = bvc_dag_get_mono64(dag, a, n, bitsize);

  return  (q << 1) | sign;
}

node_occ_t bvc_dag_mono(bvc_dag_t *dag, uint32_t *a, node_occ_t n, uint32_t bitsize) {
  uint32_t *minus_a;
  uint32_t w, sign, ka, kma;
  bvnode_t q;

  w = (bitsize + 31) >> 5; // number of words in a

  assert(64 < bitsize && bvconst_is_normalized(a, bitsize) && !bvconst_is_zero(a, w));

  if (bvconst_is_one(a, w)) return n;
  if (bvconst_is_minus_one(a, bitsize)) return negate_occ(n);

  sign = sign_of_occ(n);
  n = unsigned_occ(n);

  /*
   * Normalization: we store -a in dag->aux
   */
  bvconstant_copy(&dag->aux, bitsize, a);
  minus_a = dag->aux.data;
  bvconst_negate(minus_a, w);
  bvconst_normalize(minus_a, bitsize);

  ka = bvconst_popcount(a, w);
  kma = bvconst_popcount(minus_a, w);
  assert(1 <= ka && ka <= bitsize && 1 <= kma && kma <= bitsize);

  if (kma < ka || (kma == ka && !bvconst_tst_bit(minus_a, bitsize - 1))) {
    a = minus_a;
    sign ^= 1; // flip the sign
  }

  q = bvc_dag_get_mono(dag, a, n, bitsize);
  return (q << 1) | sign;
}



/*
 * Construct a sum node q
 * - a = array of n node occurrences
 * - n must be positive
 *
 * If n == 1, this returns a[0].
 * Otherwise, a is sorted and a node q := [sum a[0] ... a[n-1]] is created
 */
node_occ_t bvc_dag_sum(bvc_dag_t *dag, node_occ_t *a, uint32_t n, uint32_t bitsize) {
  assert(n > 0);

  if (n == 1) return a[0];

  int_array_sort(a, n);
  return bvp(bvc_dag_get_sum(dag, a, n, bitsize));
}



/*
 * Binary sum: n1 n2
 */
node_occ_t bvc_dag_sum2(bvc_dag_t *dag, node_occ_t n1, node_occ_t n2, uint32_t bitsize) {
  node_occ_t a[2];

  if (n1 < n2) {
    a[0] = n1;
    a[1] = n2;
  } else {
    a[0] = n2;
    a[1] = n1;
  }

  return bvp(bvc_dag_get_sum(dag, a, 2, bitsize));
}



/*
 * Construct a product node q
 * - q is defined by the exponents in power product p and the
 *   nodes in array a: if p is x_1^d_1 ... x_k^d_k
 *   then a must have k elements a[0] ... a[k-1]
 *   and q is [prod a[0]^d_1 ... a[k-1]^d_k]
 */
node_occ_t bvc_dag_pprod(bvc_dag_t *dag, pprod_t *p, node_occ_t *a, uint32_t bitsize) {
  pp_buffer_t *buffer;
  uint32_t i, n;

  // build the power product in dag->pp_aux
  buffer = &dag->pp_aux;
  pp_buffer_reset(buffer);
  n = p->len;
  for (i=0; i<n; i++) {
    pp_buffer_mul_varexp(buffer, a[i], p->prod[i].exp);
  }
  
  return bvp(bvc_dag_get_prod(dag, buffer->prod, buffer->len, bitsize));
}



/*
 * Binary product: n1 n2
 */
node_occ_t bvc_dag_pprod2(bvc_dag_t *dag, node_occ_t n1, node_occ_t n2, uint32_t bitsize) {
  pp_buffer_t *buffer;

  buffer = &dag->pp_aux;
  pp_buffer_reset(buffer);
  pp_buffer_set_var(buffer, n1);
  pp_buffer_mul_var(buffer, n2);

  return bvp(bvc_dag_get_prod(dag, buffer->prod, buffer->len, bitsize));
}



/*
 * Convert a polynomial p to a DAG node q and return q
 * - q is defined by the coefficients in p and the node indices
 *   in array a: if p is b_0 x_0 + b_1 x_1 + ... + b_k x_k 
 *   then a must have k+1 elements a[0] ... a[k]
 *   and q is built for (b_0 * a[0] + b_1 a[1] + ... + b_k a[k])
 *
 * - if x_0 is const_idx, then a[0] is ignored and 
 *       q is built for (b_0 + b_1 a[1] + ... + b_k a[k]).
 *
 * The DAG for p = (b0 + b_1 a[1] + .... + b_k a[k]) is 
 *    [offset b0 [sum [mono b_1 a[1]] ... [mono b_k a[k]]]].
 */
node_occ_t bvc_dag_poly64(bvc_dag_t *dag, bvpoly64_t *p, node_occ_t *a) {
  ivector_t *v;
  uint32_t i, n, bitsize;
  node_occ_t r;

  n = p->nterms;
  bitsize = p->bitsize;
  i = 0;
  if (p->mono[0].var == const_idx) {
    // skip the constant
    i = 1;
  }

  // build the monomials and store the corresponding node occs in v
  v = &dag->buffer;
  assert(v->size == 0);

  while (i < n) {
    r = bvc_dag_mono64(dag, p->mono[i].coeff, a[i], bitsize);
    ivector_push(v, r);
    i ++;
  }

  // build the sum
  r = bvc_dag_sum(dag, v->data, v->size, bitsize);
  ivector_reset(v);

  // add the constant if any
  if (p->mono[0].var == const_idx) {
    r = bvc_dag_offset64(dag, p->mono[0].coeff, r, bitsize);
  }

  return r;
}

node_occ_t bvc_dag_poly(bvc_dag_t *dag, bvpoly_t *p, node_occ_t *a) {
  ivector_t *v;
  uint32_t i, n, bitsize;
  node_occ_t r;

  n = p->nterms;
  bitsize = p->bitsize;
  i = 0;
  if (p->mono[0].var == const_idx) {
    // skip the constant
    i = 1;
  }

  // build the monomials and store the corresponding node occs in v
  v = &dag->buffer;
  assert(v->size == 0);

  while (i < n) {
    r = bvc_dag_mono(dag, p->mono[i].coeff, a[i], bitsize);
    ivector_push(v, r);
    i ++;
  }

  // build the sum
  r = bvc_dag_sum(dag, v->data, v->size, bitsize);
  ivector_reset(v);

  // add the constant if any
  if (p->mono[0].var == const_idx) {
    r = bvc_dag_offset(dag, p->mono[0].coeff, r, bitsize);
  }

  return r;  
}


/*
 * Same thing but p is stored in buffer b
 */
node_occ_t bvc_dag_poly_buffer(bvc_dag_t *dag, bvpoly_buffer_t *b, node_occ_t *a) {
  ivector_t *v;
  uint32_t nbits, i, n;
  node_occ_t r;

  n = bvpoly_buffer_num_terms(b);
  nbits = bvpoly_buffer_bitsize(b);
  i = 0;
  if (bvpoly_buffer_var(b, 0) == const_idx) {
    // skip the constant
    i = 1;
  }

  v = &dag->buffer;
  assert(v->size == 0);

  if (nbits <= 64) {
    while (i < n) {
      r = bvc_dag_mono64(dag, bvpoly_buffer_coeff64(b, i), a[i], nbits);
      ivector_push(v, r);
      i ++;
    } 
    r = bvc_dag_sum(dag, v->data, v->size, nbits);
    if (bvpoly_buffer_var(b, 0) == const_idx) {
      r = bvc_dag_offset64(dag, bvpoly_buffer_coeff64(b, 0), r, nbits);
    }

  } else {
    // same thing: bitsize > 64
    while (i < n) {
      r = bvc_dag_mono(dag, bvpoly_buffer_coeff(b, i), a[i], nbits);
      ivector_push(v, r);
      i ++;
    } 
    r = bvc_dag_sum(dag, v->data, v->size, nbits);
    if (bvpoly_buffer_var(b, 0) == const_idx) {
      r = bvc_dag_offset(dag, bvpoly_buffer_coeff(b, 0), r, nbits);
    }
  }

  ivector_reset(v);

  return r;
}



/*
 * LIST LENGTHS
 */
uint32_t bvc_num_leaves(bvc_dag_t *dag) {
  return list_length(dag->list, BVC_DAG_LEAF_LIST);
}

uint32_t bvc_num_elem_nodes(bvc_dag_t *dag) {
  return list_length(dag->list, BVC_DAG_ELEM_LIST);
}

uint32_t bvc_num_complex_nodes(bvc_dag_t *dag) {
  return list_length(dag->list, BVC_DAG_DEFAULT_LIST);
}



/*
 * REDUCTION
 */

/*
 * Check whether n1 and n2 are occurrences of the same node
 * - i.e., all bits are the same except possible bit 0
 */
static inline bool same_node(node_occ_t n1, node_occ_t n2) {
  return ((n1 ^ n2) >> 1) == 0;
}


/*
 * Remove i from the use list of n
 */
static void bvc_dag_remove_dependent(bvc_dag_t *dag, bvnode_t n, bvnode_t i) {
  int32_t *l;
  uint32_t j, k, m;

  assert(0 < n && n <= dag->nelems && 0 < i && i <= dag->nelems);

  l = dag->use[n];
  assert(l != NULL);

  m = iv_size(l);
  k = 0;
  for (j=0; j<m; j++) {
    if (l[j] != i) {
      l[k] = l[j];
      k ++;
    }
  }

  assert(k == m-1);
  index_vector_shrink(l, k);
}


/*
 * Remove i from all the use lists
 * - d = descriptor of node i
 */
static void remove_prod_from_uses(bvc_dag_t *dag, bvnode_t i, bvc_prod_t *d) {
  uint32_t j, m;

  m = d->len;
  for (j=0; j<m; j++) {
    bvc_dag_remove_dependent(dag, node_of_occ(d->prod[j].var), i);
  }
}

static void remove_sum_from_uses(bvc_dag_t *dag, bvnode_t i, bvc_sum_t *d) {
  uint32_t j, m;

  m = d->len;
  for (j=0; j<m; j++) {
    bvc_dag_remove_dependent(dag, node_of_occ(d->sum[j]), i);
  }
}

static void remove_from_uses(bvc_dag_t *dag, bvnode_t i, bvc_header_t *d) {
  assert(0 < i && i <= dag->nelems && dag->desc[i] == d);

  switch (d->tag) {
  case BVC_LEAF:
    break;

  case BVC_OFFSET:
    bvc_dag_remove_dependent(dag, node_of_occ(offset_node(d)->nocc), i);
    break;

  case BVC_MONO:
    bvc_dag_remove_dependent(dag, node_of_occ(mono_node(d)->nocc), i);
    break;

  case BVC_PROD:
    remove_prod_from_uses(dag, i, prod_node(d));
    break;

  case BVC_SUM:
    remove_sum_from_uses(dag, i, sum_node(d));
    break;

  case BVC_ALIAS:
    break;
  }
}


/*
 * Scan the dependents of a leaf node i (after i is converted to a leaf)
 * - all dependents that have become elementary are moved to the elem_list
 */
static void reclassify_dependents(bvc_dag_t *dag, bvnode_t i) {
  int32_t *l;
  uint32_t j, m;
  bvnode_t r;

  l = dag->use[i];
  if (l != NULL) {
    m = iv_size(l);
    for (j=0; j<m; j++) {
      r = l[j];
      if (node_is_elementary(dag, r)) {
	bvc_dag_move_to_elementary_list(dag, r);
      }
    }
  }
}


/*
 * Convert i to a leaf node (for variable x)
 * - i must not be a leaf node already
 */
void bvc_dag_convert_to_leaf(bvc_dag_t *dag, bvnode_t i, int32_t x) {
  bvc_header_t *d;
  bvc_leaf_t *o;
  uint32_t bitsize;
  

  assert(0 < i && i <= dag->nelems);
  d = dag->desc[i];
  assert(d->tag != BVC_LEAF);
  bitsize = d->bitsize;
  remove_from_uses(dag, i, d);
  free_descriptor(dag, d);

  o = alloc_leaf(dag);
  o->header.tag = BVC_LEAF;
  o->header.bitsize = bitsize;
  o->map = x;

  dag->desc[i] = &o->header;

  bvc_dag_move_to_leaves(dag, i);

  reclassify_dependents(dag, i);
}



/*
 * Replace i by n in descriptor d
 * - i is known to occur in d
 */
static inline void replace_node_in_offset(bvc_offset_t *d, bvnode_t i, node_occ_t n) {
  // if d->nocc == bvp(i) then d->nocc := n
  // if d->nocc == bvn(i) then d->noce := negate_off(n);
  assert(node_of_occ(d->nocc) == i);
  d->nocc = n ^ sign_of_occ(d->nocc); 
}

static inline void replace_node_in_mono(bvc_mono_t *d, bvnode_t i, node_occ_t n) {
  assert(node_of_occ(d->nocc) == i);
  d->nocc = n ^ sign_of_occ(d->nocc);
}

static void replace_node_in_sum(bvc_sum_t *d, bvnode_t i, node_occ_t n) {
  uint32_t j, m;

  m = d->len;
  for (j=0; j<m; j++) {
    if (node_of_occ(d->sum[j]) == i) break;
  }
  assert(j < m);
  d->sum[j] = n ^ sign_of_occ(d->sum[j]);
}

static void replace_node_in_prod(bvc_prod_t *d, bvnode_t i, node_occ_t n) {
  uint32_t j, m;

  m = d->len;
  for (j=0; j<m; j++) {
    if (node_of_occ(d->prod[j].var) == i) break;
  }
  assert(j < m);
  d->prod[j].var = n ^ sign_of_occ(d->prod[j].var);
}

static void replace_node_in_desc(bvc_header_t *d, bvnode_t i, node_occ_t n) {
  switch (d->tag) {
  case BVC_LEAF:
  case BVC_ALIAS:
    // should not happen
    assert(false);
    break;

  case BVC_OFFSET:
    replace_node_in_offset(offset_node(d), i, n);
    break;

  case BVC_MONO:
    replace_node_in_mono(mono_node(d), i, n);
    break;

  case BVC_SUM:
    replace_node_in_sum(sum_node(d), i, n);
    break;

  case BVC_PROD:
    replace_node_in_prod(prod_node(d), i, n);
    break;
  }
}


/*
 * Convert i to an alias node for n
 */
static void convert_to_alias(bvc_dag_t *dag, bvnode_t i, node_occ_t n) {
  bvc_header_t *d;
  bvc_alias_t *o;
  uint32_t bitsize;

  assert(0 < i && i <= dag->nelems);
  d = dag->desc[i];
  bitsize = d->bitsize;
  free_descriptor(dag, d);

  o = alloc_alias(dag);
  o->header.tag = BVC_ALIAS;
  o->header.bitsize = bitsize;
  o->alias = n;

  dag->desc[i] = &o->header;

  list_remove(dag->list, i); // remove i from leaf/elem/default lists
}



/*
 * Replace all occurrences of node i by n
 * - n must be a leaf node
 */
static void replace_node(bvc_dag_t *dag, bvnode_t i, node_occ_t n) {
  int32_t *l;
  uint32_t j, m;
  bvnode_t x;

  assert(0 < i && i <= dag->nelems);
  assert(bvc_dag_occ_is_leaf(dag, n));

  l = dag->use[i];
  if (l != NULL) {
    m = iv_size(l);
    for (j=0; j<m; j++) {
      x = l[j];
      replace_node_in_desc(dag->desc[x], i, n);
      bvc_dag_add_dependency(dag, node_of_occ(n), x);  // now x depends on n
      if (node_is_elementary(dag, x)) {
	bvc_dag_move_to_elementary_list(dag, x);
      }
    }
    delete_index_vector(l);
    dag->use[i] = NULL;
  }

  convert_to_alias(dag, i, n);
}



/*
 * SUM REDUCTION
 */

/*
 * Replace the pair n1, n2 by n in p->sum:
 * - p must be the descriptor of node i
 * - n1 and n2 must occur in p
 * - n must be a leaf
 * - remove i from n1 and n2's use lists and add i to n's use list
 * - move i to the elementary list if p becomes elementary
 */
static void shrink_sum(bvc_dag_t *dag, bvc_sum_t *p, bvnode_t i, node_occ_t n, node_occ_t n1, node_occ_t n2) {
  uint32_t j, k, m;
  node_occ_t x;

  m = p->len;

  assert(m >= 2);

  if (m == 2) {
    // i is equal to n
    assert((p->sum[0] == n1 && p->sum[1] == n2) || (p->sum[0] == n2 && p->sum[1] == n1));
    replace_node(dag, i, n);
    return;;
  }

  p->hash = 0;
  k = 0;
  for (j=0; j<m; j++) {
    x = p->sum[j];
    if (x != n1 && x != n2) {
      p->sum[k] = x;
      p->hash |= bit_hash_occ(x);
      k ++;
    }
  }

  // add n last (don't keep p->sum sorted)
  assert(k == m-2);
  p->sum[k] = n;
  p->len = k+1;
  p->hash |= bit_hash_occ(n);

  if (sum_node_is_elementary(dag, p)) {
    bvc_dag_move_to_elementary_list(dag, i);
  }

  bvc_dag_remove_dependent(dag, node_of_occ(n1), i);
  bvc_dag_remove_dependent(dag, node_of_occ(n2), i);
  bvc_dag_add_dependency(dag, node_of_occ(n), i);
}


/*
 * Check whether node i is a sum that contains +n1 and +n2 or -n1 and -n2
 * If so replace the pair n1, n2 by n in node i
 * - h = bit hash of {n1, n2}
 */
static void try_reduce_sum(bvc_dag_t *dag, bvnode_t i, uint32_t h, node_occ_t n, node_occ_t n1, node_occ_t n2) {
  bvc_header_t *d;
  bvc_sum_t *p;
  uint32_t j, m;
  int32_t k1, k2;

  assert(0 < i && i <= dag->nelems && !same_node(n1, n2));

  d = dag->desc[i];
  if (node_is_sum(d)) {
    p = sum_node(d);
    if ((h & p->hash) == h) {
      // variables v1 (for n1) and v2 (for n2) may occur in p
      m = p->len;
      k1 = -1;
      k2 = -1;
      for (j=0; j<m; j++) {
	if (same_node(n1, p->sum[j])) {
	  assert(k1 < 0);
	  k1 = j;
	} else if (same_node(n2, p->sum[j])) {
	  assert(k2 < 0);
	  k2 = j;
	}
      }

      if (k1 >= 0 && k2 >= 0) {
	// p->sum[k1] contains +/- n1
	// p->sum[k2] contains +/- n2
	if (p->sum[k1] == n1 && p->sum[k2] == n2) {
	  shrink_sum(dag, p, i, n, n1, n2);
	} else if (p->sum[k1] == negate_occ(n1) && p->sum[k2] == negate_occ(n2)) {
	  shrink_sum(dag, p, i, negate_occ(n), negate_occ(n1), negate_occ(n2));
	}
      }
    }
  }
}




/*
 * Replace all occurrences of {n1, n2} in sums by n
 * - n must be a leaf node
 */
void bvc_dag_reduce_sum(bvc_dag_t *dag, node_occ_t n, node_occ_t n1, node_occ_t n2) {
  ivector_t *v;
  int32_t *l1, *l2;
  uint32_t m, i;
  bvnode_t r1, r2;
  uint32_t h;

  r1 = node_of_occ(n1);
  r2 = node_of_occ(n2);
  h = bit_hash(r1) | bit_hash(r2);

  assert(0 < r1 && r1 <= dag->nelems && 0 < r2 && r2 <= dag->nelems && r1 != r2);

  l1 = dag->use[r1];
  l2 = dag->use[r2];

  if (l1 != NULL && l2 != NULL) {
    m = iv_size(l1);
    i = iv_size(l2);
    if (i < m) {
      m = i;
      l1 = l2;
    }

    /*
     * l1 = smallest of use[r1], use[r2]
     * m = length of l1
     */
    // copy l1 into dag->buffer since try_reduce_sum may modify l1
    v = &dag->buffer;
    ivector_copy(v, l1, m);
    for (i=0; i<m; i++) {
      try_reduce_sum(dag, v->data[i], h, n, n1, n2);
    }
    ivector_reset(v);
  }
  
}



/*
 * Check whether node i is a sum that contains n1 and n2 or -n1 and -n2
 * - h = bit hash of {n1, n2}
 */
static bool check_reduce_sum(bvc_dag_t *dag, bvnode_t i, uint32_t h, node_occ_t n1, node_occ_t n2) {
  bvc_header_t *d;
  bvc_sum_t *p;
  uint32_t j, m;
  int32_t k1, k2;

  assert(0 < i && i <= dag->nelems && !same_node(n1, n2));

  d = dag->desc[i];
  if (node_is_sum(d)) {
    p = sum_node(d);
    if ((h & p->hash) == h) {
      m = p->len;
      k1 = -1;
      k2 = -1;
      for (j=0; j<m; j++) {
	if (same_node(n1, p->sum[j])) {
	  assert(k1 < 0);
	  k1 = j;
	  if (k2 >= 0) break;
	} else if (same_node(n2, p->sum[j])) {
	  assert(k2 < 0);
	  k2 = j;
	  if (k1 >= 0) break;
	}
      }

      if (k1 >= 0 && k2 >= 0) {
	// could use more xor tricks here?
	return (p->sum[k1] == n1 && p->sum[k2] == n2) || 
	  (p->sum[k1] == negate_occ(n1) && p->sum[k2] == negate_occ(n2));
      }
    }
  }
  
  return false;
}


/*
 * Check whether there is a sum node that can be reduced by n1 + n2 or -n1 -n2
 * - n1 and n2 must be distinct 
 */
bool bvc_dag_check_reduce_sum(bvc_dag_t *dag, node_occ_t n1, node_occ_t n2) {
  int32_t *l1, *l2;
  uint32_t m, i;
  bvnode_t r1, r2;
  uint32_t h;

  r1 = node_of_occ(n1);
  r2 = node_of_occ(n2);
  h = bit_hash(r1) | bit_hash(r2);

  assert(0 < r1 && r1 <= dag->nelems && 0 < r2 && r2 <= dag->nelems && r1 != r2);

  l1 = dag->use[r1];
  l2 = dag->use[r2];

  if (l1 != NULL && l2 != NULL) {
    m = iv_size(l1);
    i = iv_size(l2);
    if (i < m) {
      m = i;
      l1 = l2;
    }

    for (i=0; i<m; i++) {
      if (check_reduce_sum(dag, l1[i], h, n1, n2)) {
	return true;
      }
    }
  }

  return false;
}




/*
 * PRODUCT REDUCTION
 */


/*
 * Find position where n occurs in p
 * - return -1 if n does not occur in p
 */
static int32_t pprod_get_index(bvc_prod_t *p, node_occ_t n) {
  uint32_t i, m;

  m = p->len;
  for (i=0; i<m; i++) {
    if (p->prod[i].var == n) {
      return i;
    }
  }

  return -1;
}



/*
 * Construct the product p * (r ^ e) then delete p
 */
static bvc_prod_t *mk_prod_times_occ_power(bvc_dag_t *dag, bvc_prod_t *p, node_occ_t r, uint32_t e) {
  bvc_prod_t *tmp;
  uint32_t i, n;

  n = p->len;
  tmp = alloc_prod(dag, n+1);
  tmp->header.tag = BVC_PROD;
  tmp->header.bitsize = p->header.bitsize;
  tmp->hash = p->hash;
  tmp->size = n+1;
  tmp->len = n+1;

  for (i=0; i<n; i++) {
    assert(p->prod[i].var != r && p->prod[i].exp > 0);
    tmp->prod[i] = p->prod[i];
  }
  tmp->prod[n].var = r;
  tmp->prod[n].exp = e;
  tmp->hash |= bit_hash_occ(r);

  free_prod(dag, p);

  return tmp;
}


/*
 * Remove all zero exponents from p and recompute the bit hash
 */
static void cleanup_prod(bvc_prod_t *p) {
  uint32_t i, j, n;

  j = 0;
  n = p->len;
  p->hash = 0;
  for (i=0; i<n; i++) {
    if (p->prod[i].exp > 0) {
      p->prod[j] = p->prod[i];
      p->hash |= bit_hash_occ(p->prod[i].var);
      j ++;
    }
  }
  p->len = j;
}


/*
 * Check whether node i is a product that contains n1 * n2
 * If so, replace the pair n1 * n2 by n in node i
 * - h must be the bit hash of {n1, n2}
 * - n1 and n2 must be distinct positive occurrences
 */
static void try_reduce_prod(bvc_dag_t *dag, bvnode_t i, uint32_t h, node_occ_t n, node_occ_t n1, node_occ_t n2) {
  bvc_header_t *d;
  bvc_prod_t *p;
  int32_t k1, k2, k;
  uint32_t e1, e2;

  assert(0 < i && i <= dag->nelems && n1 != n2);

  d = dag->desc[i];
  if (node_is_prod(d)) {
    p = prod_node(d);
    if ((h & p->hash) == h) {
      k1 = pprod_get_index(p, n1);
      k2 = pprod_get_index(p, n2);
      if (k1 >= 0 && k2 >= 0) {
	/*
	 * p contains n1^e1 * n2^e2 where e1>0 and e2>0
	 * If e1 <= e2: n1^e1 * n2^e2 --> n^e1 * n2^(e2 - e1)
	 * If e2 < e1:  n1^e1 * n2^e2 --> n^e2 * n1^(e1 - e2)
	 */
	e1 = p->prod[k1].exp;
	e2 = p->prod[k2].exp;
	if (e1 <= e2) {
	  bvc_dag_remove_dependent(dag, node_of_occ(n1), i);
	  p->prod[k1].exp = 0;
	  p->prod[k2].exp -= e1;
	  if (e1 == e2) {
	    bvc_dag_remove_dependent(dag, node_of_occ(n2), i);
	  }
	} else {
	  bvc_dag_remove_dependent(dag, node_of_occ(n2), i); 
	  p->prod[k1].exp -= e2;
	  p->prod[k2].exp = 0;
	  k1 = k2;
	  e1 = e2;
	}

	// increase exponent of n by e1
	assert(p->prod[k1].exp == 0);
	k = pprod_get_index(p, n);
	if (k >= 0) {
	  p->prod[k].exp += e1;
	} else {
	  bvc_dag_add_dependency(dag, node_of_occ(n), i);
	  // store n^e1 at index k1
	  p->prod[k1].var = n;
	  p->prod[k1].exp = e1;
	}

	cleanup_prod(p);	    
	if (prod_node_is_elementary(dag, p)) {
	  bvc_dag_move_to_elementary_list(dag, i);
	}
      }
    }

  }
}



/*
 * Check whether node i is a product that contains n1^2
 * If so replace n1^2 by n in node i
 * - h must be the hash of n1
 */
static void try_reduce_square(bvc_dag_t *dag, bvnode_t i, uint32_t h, node_occ_t n, node_occ_t n1) {
  bvc_header_t *d;
  bvc_prod_t *p;
  int32_t k1, k;
  uint32_t e;

  assert(0 < i && i <= dag->nelems);

  d = dag->desc[i];
  if (node_is_prod(d)) {
    p = prod_node(d);
    if ((h & p->hash) == h) {
      k1 = pprod_get_index(p, n1);
      e = p->prod[k1].exp;
      if (k1 >= 0 && e >= 2) {
	/*
	 * p contains n1^e with e >= 2
	 * If e is 2t+1: n1^e ---> n1 * n^t
	 * If e is 2t:   n1^e ---> n^t
	 */
	if ((e & 1) == 0) {
	  p->prod[k1].exp = 0;
	  bvc_dag_remove_dependent(dag, node_of_occ(n1), i);	  
	} else {
	  p->prod[k1].exp = 1;
	}

	e >>= 1;
	k = pprod_get_index(p, n);
	if (k >= 0) {
	  p->prod[k].exp += e;
	  cleanup_prod(p);
	} else {
	  bvc_dag_add_dependency(dag, node_of_occ(n), i);
	  if (p->prod[k1].exp == 0) {
	    // store n^e at index k1
	    p->prod[k1].var = n;
	    p->prod[k1].exp = e;
	    cleanup_prod(p);
	  } else {
	    p = mk_prod_times_occ_power(dag, p, n, e);
	    dag->desc[i] = &p->header;
	  }
	}

	if (prod_node_is_elementary(dag, p)) {
	  bvc_dag_move_to_elementary_list(dag, i);
	}
      }

    }
  }
}



/*
 * Replace all occurrences of {n1, n2} in products by n
 */
void bvc_dag_reduce_prod(bvc_dag_t *dag, node_occ_t n, node_occ_t n1, node_occ_t n2) {
  ivector_t *v;
  int32_t *l1, *l2;
  uint32_t m, i;
  bvnode_t r1, r2;
  uint32_t h;

  r1 = node_of_occ(n1);
  r2 = node_of_occ(n2);
  h = bit_hash(r1) | bit_hash(r2);

  assert(0 < r1 && r1 <= dag->nelems && 0 < r2 && r2 <= dag->nelems);

  l1 = dag->use[r1];
  l2 = dag->use[r2];

  if (l1 != NULL && l2 != NULL) {
    m = iv_size(l1);
    i = iv_size(l2);
    if (i < m) {
      m = i;
      l1 = l2;
    }

    /*
     * l1 = smallest of use[r1], use[r2]
     * m = length of l1
     */
    // copy l1 into dag->buffer since try_reduce_sum may modify l1
    v = &dag->buffer;
    ivector_copy(v, l1, m);

    if (n1 == n2) {
      for (i=0; i<m; i++) {
	try_reduce_square(dag, v->data[i], h, n, n1);
      }
    } else {
      for (i=0; i<m; i++) {
	try_reduce_prod(dag, v->data[i], h, n, n1, n2);
      }
    }

    ivector_reset(v);
  }
  
}



/*
 * Check whether i is a polynomial that contains n1 * n2 as a subproduct
 * - h = bit_hash of {n1,  n2}
 */
static bool check_reduce_prod(bvc_dag_t *dag, bvnode_t i, uint32_t h, node_occ_t n1, node_occ_t n2) {
  bvc_header_t *d;
  bvc_prod_t *p;

  assert(0 < i && i <= dag->nelems && n1 != n2);

  d = dag->desc[i];
  if (node_is_prod(d)) {
    p = prod_node(d);
    if ((h & p->hash) == h) {
      return pprod_get_index(p, n1) >= 0 && pprod_get_index(p, n2) >= 0;
    }
  }

  return false;
}


/*
 * Check whether i is a polynomial that contains n1^2 as a subproduct
 * - h = bit_hash of {n1}
 */
static bool check_reduce_square(bvc_dag_t *dag, bvnode_t i, uint32_t h, node_occ_t n1) {
  bvc_header_t *d;
  bvc_prod_t *p;
  int32_t k;

  assert(0 < i && i <= dag->nelems);

  d = dag->desc[i];
  if (node_is_prod(d)) {
    p = prod_node(d);
    if ((h & p->hash) == h) {
      k = pprod_get_index(p, n1);
      return k >= 0 && p->prod[k].exp >= 2;
    }
  }

  return false;
}


/*
 * Check whether there's a product node that can be reduced by n1 * n2
 */
bool bvc_dag_check_reduce_prod(bvc_dag_t *dag, node_occ_t n1, node_occ_t n2) {
  int32_t *l1, *l2;
  uint32_t m, i;
  bvnode_t r1, r2;
  uint32_t h;

  r1 = node_of_occ(n1);
  r2 = node_of_occ(n2);
  h = bit_hash(r1) | bit_hash(r2);

  assert(0 < r1 && r1 <= dag->nelems && 0 < r2 && r2 <= dag->nelems);

  l1 = dag->use[r1];
  l2 = dag->use[r2];

  if (l1 != NULL && l2 != NULL) {
    m = iv_size(l1);
    i = iv_size(l2);
    if (i < m) {
      m = i;
      l1 = l2;
    }

    /*
     * l1 = smallest of use[r1], use[r2]
     * m = length of l1
     */
    if (n1 == n2) {
      for (i=0; i<m; i++) {
	if (check_reduce_square(dag, l1[i], h, n1)) {
	  return true;
	}
      }
    } else {
      for (i=0; i<m; i++) {
	if (check_reduce_prod(dag, l1[i], h, n1, n2)) {
	  return true;
	}
      }
    }
  } 
  

  return false;
}
