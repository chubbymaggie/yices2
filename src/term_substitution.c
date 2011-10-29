/*
 * Data structure to support term substitution
 */

#include <assert.h>

#include "memalloc.h"
#include "arith_buffer_terms.h"
#include "bvarith_buffer_terms.h"
#include "bvarith64_buffer_terms.h"

#include "term_substitution.h"



/*
 * Check whether arrays v and t define a valid substitution:
 * - v and t must be arrays of n terms 
 * - this returns true if forall i, v[i] is a variable
 *   and the type of t[i] is a subtype of v[i]'s type.
 */
bool good_term_subst(term_table_t *terms, uint32_t n, term_t *v, term_t *t) {
  type_table_t *types;
  uint32_t i;
  term_t x, u;  

  types = terms->types;
  for (i=0; i<n; i++) {
    x = v[i];
    u = t[i];
    assert(good_term(terms, x) && good_term(terms, u));
    if (is_neg_term(x) || term_kind(terms, x) != VARIABLE || 
	!is_subtype(types, term_type(terms, u), term_type(terms, x))) {
      return false;
    }
  }

  return true;
}


/*
 * Initialize subst: store the mapping v[i] --> t[i]
 */
void init_term_subst(term_subst_t *subst, term_manager_t *mngr, uint32_t n, term_t *v, term_t *t) {
  int_hmap_pair_t *p;
  uint32_t i;
  term_t x;

  assert(good_term_subst(term_manager_get_terms(mngr), n, v, t));

  subst->mngr = mngr;
  subst->terms = term_manager_get_terms(mngr);
  init_int_hmap(&subst->map, 0);
  init_subst_cache(&subst->cache);
  init_istack(&subst->stack);
  subst->rctx = NULL;
  subst->fvar = NULL;
  
  for (i=0; i<n; i++) {
    x = v[i];
    assert(is_pos_term(x) && term_kind(subst->terms, x) == VARIABLE && 
	   good_term(subst->terms, t[i]));
    p = int_hmap_get(&subst->map, x);
    p->val = t[i];
  }
}



/*
 * Get the renaming context. Allocate and initialize it if needed.
 */
static renaming_ctx_t *term_subst_get_rctx(term_subst_t *subst) {
  renaming_ctx_t *tmp;

  tmp = subst->rctx;
  if (tmp == NULL) {
    tmp = (renaming_ctx_t *) safe_malloc(sizeof(renaming_ctx_t));
    init_renaming_ctx(tmp, subst->terms, 0);
    subst->rctx = tmp;
  }

  return tmp;
}


/*
 * Get the variable collector: allocate and initialize it if needed
 */
static fvar_collector_t *term_subst_get_fvar(term_subst_t *subst) {
  fvar_collector_t *tmp;

  tmp = subst->fvar;
  if (tmp == NULL) {
    tmp = (fvar_collector_t *) safe_malloc(sizeof(fvar_collector_t));
    init_fvar_collector(tmp, subst->terms);
    subst->fvar = tmp;
  }

  return tmp;
}


/*
 * Delete: free all memory used
 */
void delete_term_subst(term_subst_t *subst) {
  delete_int_hmap(&subst->map);
  delete_subst_cache(&subst->cache);
  delete_istack(&subst->stack);
  if (subst->rctx != NULL) {
    delete_renaming_ctx(subst->rctx);
    safe_free(subst->rctx);
    subst->rctx = NULL;
  }
  if (subst->fvar != NULL) {
    delete_fvar_collector(subst->fvar);
    safe_free(subst->fvar);
    subst->fvar = NULL;
  }
}



/*
 * UTILITIES
 */

/*
 * Lookup the term mapped to x (taking renaming into account)
 * - x must be a variable
 * - if there's a renaming, lookup x in the renaming context
 * - if x is not renamed, lookup x in the map
 * - if x is not renamed and not in the map, then return x
 */
static term_t get_subst_of_var(term_subst_t *subst, term_t x) {
  int_hmap_pair_t *p;
  term_t y;

  assert(is_pos_term(x) && term_kind(subst->terms, x) == VARIABLE);

  y = NULL_TERM;
  if (subst->rctx != NULL) {
    y = renaming_ctx_lookup(subst->rctx, x);
  }
  if (y == NULL_TERM) {
    p = int_hmap_find(&subst->map, x);
    y = x;
    if (p != NULL) {
      y = p->val;
    }
  }

  return y;
}


/*
 * Check whether the result of subst(t) is stored in the cache
 * - t must be a valid term in subst->terms
 * - this takes the renaming context into account
 * - return NULL_TERM (-1) is the result is not in the cache
 */
static term_t get_cached_subst(term_subst_t *subst, term_t t) {
  renaming_ctx_t *ctx;
  harray_t *hashed_ctx;

  assert(is_pos_term(t) && good_term(subst->terms, t));

  hashed_ctx = NULL;
  ctx = subst->rctx;
  if (ctx != NULL && !renaming_ctx_is_empty(ctx)) {
    hashed_ctx = renaming_ctx_hash(ctx);
  }

  return subst_cache_lookup(&subst->cache, hashed_ctx, t);
} 


/*
 * Add u as subst(t) in the cache
 * - t and u must be valid terms in subst->terms
 * - there must not be an existing value for t in the cache
 * - this takes the renaming context into account (cf. subst_cache.h)
 */
static void cache_subst_result(term_subst_t *subst, term_t t, term_t u) {
  renaming_ctx_t *ctx;
  harray_t *hashed_ctx;

  assert(is_pos_term(t) && good_term(subst->terms, t) && good_term(subst->terms, u));

  hashed_ctx = NULL;
  ctx = subst->rctx;
  if (ctx != NULL && !renaming_ctx_is_empty(ctx)) {
    hashed_ctx = renaming_ctx_hash(ctx);
  }

  subst_cache_add(&subst->cache, hashed_ctx, t, u);
}


/*
 * Extend the current renaming context: by renamings for v[0 .. n-1]
 * (cf. renaming_context.h)
 * - v[0] ... v[n-1] must all be variables in subst->terms
 * - this allocates and initializes the renaming data structure if needed
 */
static void subst_push_renaming(term_subst_t *subst, uint32_t n, term_t *v) {
  renaming_ctx_t *ctx;

  ctx = term_subst_get_rctx(subst);
  renaming_ctx_push_vars(ctx, n, v);
}


/*
 * Remove the last n variable renaming (cf. renaming_context.h)
 * - the current renaming must exist and have at least n variables
 */
static void subst_pop_renaming(term_subst_t *subst, uint32_t n) {
  assert(subst->rctx != NULL);
  renaming_ctx_pop_vars(subst->rctx, n);
}


/*
 * Check whether term t is ground
 * - t must be a valid term in subst->terms
 * - allocate and initialize the variable collector structure if needed
 */
static bool subst_term_is_ground(term_subst_t *subst, term_t t) {
  fvar_collector_t *fvar;

  fvar = term_subst_get_fvar(subst);
  return term_is_ground(fvar, t);
}




/*
 * BUILD POWER PRODUCTS AND POLYNOMIAL TERMS
 */

/*
 * Check whether the product a[0]^e_0 ... a[n-1]^e_{n-1} has degree > YICEX_MAX_DEGREE
 * - e_i = exponent in pprod p
 */
static bool pprod_degree_overflows(term_table_t *tbl, pprod_t *p, uint32_t n, term_t *a) {
  uint64_t d;
  uint32_t i;

  assert(n == p->len);

  d = 0;
  for (i=0; i<n; i++) {
    d += ((uint64_t) term_degree(tbl, a[i])) * p->prod[i].exp;
    if (d > (uint64_t) YICES_MAX_DEGREE) {
      return true;
    }
  }

  return false;
}


/*
 * Check whether term t is 0
 * - t is either an arithmetic or a bitvector term
 */
static bool term_is_zero(term_table_t *tbl, term_t t) {
  bvconst_term_t *c;
  uint32_t k;

  assert(is_arithmetic_term(tbl, t) || is_bitvector_term(tbl, t));

  switch (term_kind(tbl, t)) {
  case ARITH_CONSTANT:
    assert(t == zero_term || q_is_nonzero(rational_term_desc(tbl, t)));;
    return t == zero_term;

  case BV64_CONSTANT:
    return bvconst64_term_desc(tbl, t)->value == (uint64_t) 0;

  case BV_CONSTANT:
    c = bvconst_term_desc(tbl, t);
    k = (c->bitsize + 31) >> 5;
    return bvconst_is_zero(c->data, k);

  default:
    return false;
  }
}


/*
 * Arithmetic product:
 * - p is a power product descriptor: t_0^e_0 ... t_{n-1}^e_{n-1}
 * - a is an array of n arithmetic terms: a[i] = subst of t_i
 * - this function constructs the term a[0]^e_0 ... a[n-1]^e_{n-1}
 */
static term_t arith_pprod(term_manager_t *mngr, pprod_t *p, uint32_t n, term_t *a) {
  arith_buffer_t *b;
  term_table_t *tbl;
  uint32_t i;

  assert(n == p->len);

  tbl = term_manager_get_terms(mngr);
  b = term_manager_get_arith_buffer(mngr);

  arith_buffer_set_one(b); // b := 1
  for (i=0; i<n; i++) {
    // b := b * a[i]^e[i]
    arith_buffer_mul_term_power(b, tbl, a[i], p->prod[i].exp); 
  }

  return mk_arith_term(mngr, b);
}


/*
 * Bitvector product: 1 to 64 bits vector
 * - p is a power product descriptor: t_0^e_0 ... t_{n-1}^e_{n-1}
 * - a is an array of n arithmetic terms: a[i] = subst of t_i
 * - nbits = number of bits in each term
 * - this function constructs the term a[0]^e_0 ... a[n-1]^e_{n-1}
 */
static term_t bvarith64_pprod(term_manager_t *mngr, pprod_t *p, uint32_t n, term_t *a, uint32_t nbits) {
  bvarith64_buffer_t *b;
  term_table_t *tbl;
  uint32_t i;

  assert(n == p->len && 0 < nbits && nbits <= 64);

  tbl = term_manager_get_terms(mngr);
  b = term_manager_get_bvarith64_buffer(mngr);
  
  bvarith64_buffer_prepare(b, nbits);
  bvarith64_buffer_set_one(b); // b := 1
  for (i=0; i<n; i++) {
    // b := b * a[i]^e[i]
    bvarith64_buffer_mul_term_power(b, tbl, a[i], p->prod[i].exp); 
  }

  return mk_bvarith64_term(mngr, b);
}


/*
 * Bitvector product: more than 64 bits
 * - p is a power product descriptor: t_0^e_0 ... t_{n-1}^e_{n-1}
 * - a is an array of n arithmetic terms: a[i] = subst of t_i
 * - nbits = number of bits in each term
 * - this function constructs the term a[0]^e_0 ... a[n-1]^e_{n-1}
 */
static term_t bvarith_pprod(term_manager_t *mngr, pprod_t *p, uint32_t n, term_t *a, uint32_t nbits) {
  bvarith_buffer_t *b;
  term_table_t *tbl;
  uint32_t i;

  assert(n == p->len && 64 < nbits && nbits <= YICES_MAX_BVSIZE);

  tbl = term_manager_get_terms(mngr);
  b = term_manager_get_bvarith_buffer(mngr);
  
  bvarith_buffer_prepare(b, nbits);
  bvarith_buffer_set_one(b); // b := 1
  for (i=0; i<n; i++) {
    // b := b * a[i]^e[i]
    bvarith_buffer_mul_term_power(b, tbl, a[i], p->prod[i].exp); 
  }

  return mk_bvarith_term(mngr, b);
}


/*
 * Polynomial:
 * - p is a polynomial c_0 t_0 + c_1 t_1 + ... + c_{n-1} t_{n-1}
 * - a is an array of n terms: a[i] = subst of t_i
 *   except that a[0] = const_idx if t_0 = const_idx 
 * - construct the term c_0 a[0] + c_1 a[1] + ... + c_{n-1} a[n-1]
 *   (or c_0 + c_1 a[1] + ... + c_{n-1} a[n-1] if a[0] = const_idx)
 */
static term_t build_arith_poly(term_manager_t *mngr, polynomial_t *p, uint32_t n, term_t *a) {
  arith_buffer_t *b;
  term_table_t *tbl;
  uint32_t i;

  assert(p->nterms == n);

  tbl = term_manager_get_terms(mngr);
  b = term_manager_get_arith_buffer(mngr);
  arith_buffer_reset(b);

  // deal with the constant
  i = 0;
  if (a[i] == const_idx) {
    assert(p->mono[0].var == const_idx);
    arith_buffer_add_const(b, &p->mono[0].coeff);
    i ++;
  }

  // rest
  while (i < n) {
    arith_buffer_add_const_times_term(b, tbl, &p->mono[i].coeff, a[i]);
    i ++;
  }

  return mk_arith_term(mngr, b);
}


/*
 * Same thing for a bitvector polynomial (1 to 64bits)
 */
static term_t build_bvarith64_poly(term_manager_t *mngr, bvpoly64_t *p, uint32_t n, term_t *a) {
  bvarith64_buffer_t *b;
  term_table_t *tbl;
  uint32_t i;

  assert(p->nterms == n && 0 < p->bitsize && p->bitsize <= 64);

  tbl = term_manager_get_terms(mngr);
  b = term_manager_get_bvarith64_buffer(mngr);
  bvarith64_buffer_prepare(b, p->bitsize);

  // deal with the constant
  i = 0;
  if (a[i] == const_idx) {
    assert(p->mono[0].var == const_idx);
    bvarith64_buffer_add_const(b, p->mono[0].coeff);
    i ++;
  }

  // rest
  while (i < n) {
    bvarith64_buffer_add_const_times_term(b, tbl, p->mono[i].coeff, a[i]);
    i ++;
  }

  return mk_bvarith64_term(mngr, b);
}


/*
 * Same thing for a bitvector polynomial (more than 64bits)
 */
static term_t build_bvarith_poly(term_manager_t *mngr, bvpoly_t *p, uint32_t n, term_t *a) {
  bvarith_buffer_t *b;
  term_table_t *tbl;
  uint32_t i;

  assert(p->nterms == n && 64 < p->bitsize && p->bitsize <= YICES_MAX_BVSIZE);

  tbl = term_manager_get_terms(mngr);
  b = term_manager_get_bvarith_buffer(mngr);
  bvarith_buffer_prepare(b, p->bitsize);

  // deal with the constant
  i = 0;
  if (a[i] == const_idx) {
    assert(p->mono[0].var == const_idx);
    bvarith_buffer_add_const(b, p->mono[0].coeff);
    i ++;
  }

  // rest
  while (i < n) {
    bvarith_buffer_add_const_times_term(b, tbl, p->mono[i].coeff, a[i]);
    i ++;
  }

  return mk_bvarith_term(mngr, b);
}


/*
 * APPLY SUBSTITUTION
 */

/*
 * Main recursive function
 * - apply 'subst' to 't': return the result
 * 
 * Error codes returned:
 * - if the substitution creates a term of degree > YICES_MAX_DEGREE
 *   abort by calling longjmp(subst->env): return -1 (NULL_TERM)
 * - if something else goes wrong (either because the subsitution is wrong
 *   or there's a bug somewhere): return -2
 */
static term_t get_subst(term_subst_t *subst, term_t t);


/*
 * Apply substitution to 'FORALL x1 ... x_n: b'
 * - d has arity n+1 >= 2
 * - d->arg[0] ... d->arg[n-1] = variables x_1 ... x_n
 * - d->arg[n] = body b
 */
static term_t subst_forall(term_subst_t *subst, composite_term_t *d) {
  term_t result;
  int32_t *a;
  uint32_t n;

  assert(d->arity >= 2);

  n = d->arity - 1;
  
  // add renaming for variables x1 ... x_n to x'1 ... x'n
  subst_push_renaming(subst, n, d->arg); 
  assert(subst->rctx != NULL);

  // store the new variables x'1 ... x'k in array a
  a = alloc_istack_array(&subst->stack, n); 
  renaming_ctx_collect_new_vars(subst->rctx, n, a);

  // build (FORALL x'1 ... x'k: subst(body))
  result = get_subst(subst, d->arg[n]);
  result = mk_forall(subst->mngr, n, a, result); 

  // cleanup
  free_istack_array(&subst->stack, a);
  subst_pop_renaming(subst, n); 

  return result;
}



/*
 * Auxiliary function: apply subst recursively to all children of d
 * - return the result in array a, allocated in the subst->stack
 */
static term_t *subst_children(term_subst_t *subst, composite_term_t *d) {
  term_t *a;
  uint32_t i, n;

  n = d->arity;
  a = alloc_istack_array(&subst->stack, n);
  for (i=0; i<n; i++) {
    a[i] = get_subst(subst, d->arg[i]);
  }

  return a;
}


/*
 * Substitution for all other composite terms
 */

// t = 0
static term_t subst_arith_eq(term_subst_t *subst, term_t t) {
  term_t u;

  u = get_subst(subst, t);  
  return mk_arith_term_eq0(subst->mngr, u);
}

// t >= 0
static term_t subst_arith_ge(term_subst_t *subst, term_t t) {
  term_t u;

  u = get_subst(subst, t);  
  return mk_arith_term_geq0(subst->mngr, u);
}

// (ite c t1 t2)
static term_t subst_ite(term_subst_t *subst, composite_term_t *d) {
  term_table_t *tbl;
  term_t c, t1, t2;
  type_t tau;
  term_t result;

  assert(d->arity == 3);

  c = get_subst(subst, d->arg[0]); // condition
  if (c == true_term) {
    result = get_subst(subst, d->arg[1]); 
  } else if (c == false_term) {
    result = get_subst(subst, d->arg[2]);
  } else {
    t1 = get_subst(subst, d->arg[1]); 
    t2 = get_subst(subst, d->arg[2]);

    tbl = subst->terms;
    tau = super_type(tbl->types, term_type(tbl, t1), term_type(tbl, t2));
    assert(tau != NULL_TYPE);

    result = mk_ite(subst->mngr, c, t1, t2, tau);
  }

  return result;
}

// function application
static term_t subst_app(term_subst_t *subst, composite_term_t *d) {
  term_t *a;
  term_t result;

  assert(d->arity >= 2);

  // a[0] = function, a[1 ... n-1] = arguments
  a = subst_children(subst, d);
  result = mk_application(subst->mngr, a[0], d->arity-1, a+1);
  free_istack_array(&subst->stack, a);

  return result;
}

// function update
static term_t subst_update(term_subst_t *subst, composite_term_t *d) {
  term_t *a;
  term_t result;
  uint32_t n;

  assert(d->arity >= 3);

  // a[0] = function, a[1..n-2] = argument, a[n-1] = new_v
  a = subst_children(subst, d);
  n = d->arity;
  result = mk_update(subst->mngr, a[0], n-2, a+1, a[n-1]);
  free_istack_array(&subst->stack, a);

  return result;  
}

// tuple
static term_t subst_tuple(term_subst_t *subst, composite_term_t *d) {
  term_t *a;
  term_t result;

  a = subst_children(subst, d);
  result = mk_tuple(subst->mngr, d->arity, a);
  free_istack_array(&subst->stack, a);

  return result;
}

// equality
static term_t subst_eq(term_subst_t *subst, composite_term_t *d) {
  term_t t, u;

  assert(d->arity == 2);
  t = get_subst(subst, d->arg[0]);
  u = get_subst(subst, d->arg[1]);
  return mk_eq(subst->mngr, t, u);
}

// distinct
static term_t subst_distinct(term_subst_t *subst, composite_term_t *d) {
  term_t *a;
  term_t result;

  a = subst_children(subst, d);
  result = mk_distinct(subst->mngr, d->arity, a);
  free_istack_array(&subst->stack, a);

  return result;
}

// or
static term_t subst_or(term_subst_t *subst, composite_term_t *d) {
  term_t *a;
  term_t result;
  uint32_t i, n;

  n = d->arity;
  assert(n >= 2);

  a = alloc_istack_array(&subst->stack, n);
  result = true_term;
  for (i=0; i<n; i++) {
    a[i] = get_subst(subst, d->arg[i]);
    if (a[i] == true_term) goto done; // result is true_term
  }

  // no simplification:
  result = mk_or(subst->mngr, n, a);

 done:
  free_istack_array(&subst->stack, a);

  return result;
}

// xor
static term_t subst_xor(term_subst_t *subst, composite_term_t *d) {
  term_t *a;
  term_t result;

  assert(d->arity >= 2);
  a = subst_children(subst, d);
  result = mk_xor(subst->mngr, d->arity, a);
  free_istack_array(&subst->stack, a);

  return result;
}

// (eq t1 t2): arithmetic 
static term_t subst_arith_bineq(term_subst_t *subst, composite_term_t *d) {
  term_t t1, t2;

  assert(d->arity == 2);
  t1 = get_subst(subst, d->arg[0]);
  t2 = get_subst(subst, d->arg[1]);
  return mk_arith_eq(subst->mngr, t1, t2);
}

// array of bits
static term_t subst_bvarray(term_subst_t *subst, composite_term_t *d) {
  term_t *a;
  term_t result;

  assert(d->arity >= 1);
  a = subst_children(subst, d);
  result = mk_bvarray(subst->mngr, d->arity, a);
  free_istack_array(&subst->stack, a);

  return result;
}

// bitvector division and shift operators
static term_t subst_bvdiv(term_subst_t *subst, composite_term_t *d) {
  term_t t1, t2;

  assert(d->arity == 2);
  t1 = get_subst(subst, d->arg[0]);
  t2 = get_subst(subst, d->arg[1]);
  return mk_bvdiv(subst->mngr, t1, t2);
}

static term_t subst_bvrem(term_subst_t *subst, composite_term_t *d) {
  term_t t1, t2;

  assert(d->arity == 2);
  t1 = get_subst(subst, d->arg[0]);
  t2 = get_subst(subst, d->arg[1]);
  return mk_bvrem(subst->mngr, t1, t2);
}

static term_t subst_bvsdiv(term_subst_t *subst, composite_term_t *d) {
  term_t t1, t2;

  assert(d->arity == 2);
  t1 = get_subst(subst, d->arg[0]);
  t2 = get_subst(subst, d->arg[1]);
  return mk_bvsdiv(subst->mngr, t1, t2);
}

static term_t subst_bvsrem(term_subst_t *subst, composite_term_t *d) {
  term_t t1, t2;

  assert(d->arity == 2);
  t1 = get_subst(subst, d->arg[0]);
  t2 = get_subst(subst, d->arg[1]);
  return mk_bvsrem(subst->mngr, t1, t2);
}

static term_t subst_bvsmod(term_subst_t *subst, composite_term_t *d) {
  term_t t1, t2;

  assert(d->arity == 2);
  t1 = get_subst(subst, d->arg[0]);
  t2 = get_subst(subst, d->arg[1]);
  return mk_bvsmod(subst->mngr, t1, t2);
}

static term_t subst_bvshl(term_subst_t *subst, composite_term_t *d) {
  term_t t1, t2;

  assert(d->arity == 2);
  t1 = get_subst(subst, d->arg[0]);
  t2 = get_subst(subst, d->arg[1]);
  return mk_bvshl(subst->mngr, t1, t2);
}

static term_t subst_bvlshr(term_subst_t *subst, composite_term_t *d) {
  term_t t1, t2;

  assert(d->arity == 2);
  t1 = get_subst(subst, d->arg[0]);
  t2 = get_subst(subst, d->arg[1]);
  return mk_bvlshr(subst->mngr, t1, t2);
}

static term_t subst_bvashr(term_subst_t *subst, composite_term_t *d) {
  term_t t1, t2;

  assert(d->arity == 2);
  t1 = get_subst(subst, d->arg[0]);
  t2 = get_subst(subst, d->arg[1]);
  return mk_bvashr(subst->mngr, t1, t2);
}

// bitvector atoms
static term_t subst_bveq(term_subst_t *subst, composite_term_t *d) {
  term_t t1, t2;

  assert(d->arity == 2);
  t1 = get_subst(subst, d->arg[0]);
  t2 = get_subst(subst, d->arg[1]);
  return mk_bveq(subst->mngr, t1, t2);
}

static term_t subst_bvge(term_subst_t *subst, composite_term_t *d) {
  term_t t1, t2;

  assert(d->arity == 2);
  t1 = get_subst(subst, d->arg[0]);
  t2 = get_subst(subst, d->arg[1]);
  return mk_bvge(subst->mngr, t1, t2);
}

static term_t subst_bvsge(term_subst_t *subst, composite_term_t *d) {
  term_t t1, t2;

  assert(d->arity == 2);
  t1 = get_subst(subst, d->arg[0]);
  t2 = get_subst(subst, d->arg[1]);
  return mk_bvsge(subst->mngr, t1, t2);
}

// projection and bit select
static term_t subst_select(term_subst_t *subst, select_term_t *d) {
  term_t t;

  t = get_subst(subst, d->arg);
  return mk_select(subst->mngr, d->idx, t);
}

static term_t subst_bit_select(term_subst_t *subst, select_term_t *d) {
  term_t t;

  t = get_subst(subst, d->arg);
  return mk_bitextract(subst->mngr, t, d->idx);
}


/*
 * Power products
 */
static term_t subst_pprod(term_subst_t *subst, pprod_t *p, type_t tau) {
  term_t *a;
  term_t result;
  uint32_t i, n, nbits;

  n = p->len;
  a = alloc_istack_array(&subst->stack, n);
  
  /*
   * p is t_0^e_0 ... t_{n-1}^e_{n-1}
   * compute a[i] = subst(t_i), stop if a[i] is zero
   */
  for (i=0; i<n; i++) {
    a[i] = get_subst(subst, p->prod[i].var);
    if (term_is_zero(subst->terms, a[i])) {
      // the result is zero
      result = a[i];
      goto done;
    }
  }

  /*
   * Check for overflow
   */
  if (pprod_degree_overflows(subst->terms, p, n, a)) {
    longjmp(subst->env, -1); // raise an exception
  }

  /*
   * build the term a[0] ^ e_0 ... a[n-1]^ e_{n-1}
   */
  if (is_arithmetic_type(tau)) {
    result = arith_pprod(subst->mngr, p, n, a);
  } else {
    nbits = bv_type_size(subst->terms->types, tau);
    if (nbits <= 64) {
      result = bvarith64_pprod(subst->mngr, p, n, a, nbits);
    } else {
      result = bvarith_pprod(subst->mngr, p, n, a, nbits);
    }
  }

 done:
  free_istack_array(&subst->stack, a);

  return result;
}


/*
 * Arithmetic polynomial
 */
static term_t subst_poly(term_subst_t *subst, polynomial_t *p) {
  term_t *a;
  term_t result;
  uint32_t i, n;

  n = p->nterms;
  a = alloc_istack_array(&subst->stack, n);
  
  // skip the constant term if any
  i = 0;
  if (p->mono[0].var == const_idx) {
    a[i] = const_idx;
    i ++;
  }

  // rest of the terms in p
  while (i < n) {
    a[i] = get_subst(subst, p->mono[i].var);
    i ++;
  }

  // build the polynomial
  result = build_arith_poly(subst->mngr, p, n, a);

  free_istack_array(&subst->stack, a);

  return result;
}


/*
 * Bitvector polynomial: 64bit coefficients
 */
static term_t subst_bvpoly64(term_subst_t *subst, bvpoly64_t *p) {
  term_t *a;
  term_t result;
  uint32_t i, n;

  n = p->nterms;
  a = alloc_istack_array(&subst->stack, n);
  
  // skip the constant term if any
  i = 0;
  if (p->mono[0].var == const_idx) {
    a[i] = const_idx;
    i ++;
  }

  // rest of the terms in p
  while (i < n) {
    a[i] = get_subst(subst, p->mono[i].var);
    i ++;
  }

  // build the polynomial
  result = build_bvarith64_poly(subst->mngr, p, n, a);

  free_istack_array(&subst->stack, a);

  return result;
}


/*
 * Bitvector polynomial: more than 64bits
 */
static term_t subst_bvpoly(term_subst_t *subst, bvpoly_t *p) {
  term_t *a;
  term_t result;
  uint32_t i, n;

  n = p->nterms;
  a = alloc_istack_array(&subst->stack, n);
  
  // skip the constant term if any
  i = 0;
  if (p->mono[0].var == const_idx) {
    a[i] = const_idx;
    i ++;
  }

  // rest of the terms in p
  while (i < n) {
    a[i] = get_subst(subst, p->mono[i].var);
    i ++;
  }

  // build the polynomial
  result = build_bvarith_poly(subst->mngr, p, n, a);

  free_istack_array(&subst->stack, a);

  return result;
}



/*
 * Core function: recursively compute subst for a composite term t
 * - t has positive polarity
 * - t is a valid term in the term table
 * - t is a composite term
 */
static term_t subst_composite(term_subst_t *subst, term_t t) {
  term_table_t *terms;
  term_t result;

  terms = subst->terms;
  assert(good_term(terms, t) && is_pos_term(t));

  switch (term_kind(terms, t)) {
  case ARITH_EQ_ATOM:
    result = subst_arith_eq(subst, arith_eq_arg(terms, t));
    break;

  case ARITH_GE_ATOM:
    result = subst_arith_ge(subst, arith_ge_arg(terms, t));
    break;

  case ITE_TERM:
  case ITE_SPECIAL:
    result = subst_ite(subst, ite_term_desc(terms, t));
    break;
		       
  case APP_TERM:
    result = subst_app(subst, app_term_desc(terms, t));
    break;

  case UPDATE_TERM:
    result = subst_update(subst, update_term_desc(terms, t));
    break;

  case TUPLE_TERM:
    result = subst_tuple(subst, tuple_term_desc(terms, t));
    break;

  case EQ_TERM:
    result = subst_eq(subst, eq_term_desc(terms, t));
    break;

  case DISTINCT_TERM:
    result = subst_distinct(subst, distinct_term_desc(terms, t));
    break;

  case FORALL_TERM:
    result = subst_forall(subst, forall_term_desc(terms, t)); 
    break;

  case OR_TERM:
    result = subst_or(subst, or_term_desc(terms, t));
    break;

  case XOR_TERM:
    result = subst_xor(subst, xor_term_desc(terms, t));
    break;

  case ARITH_BINEQ_ATOM:
    result = subst_arith_bineq(subst, arith_bineq_atom_desc(terms, t));
    break;

  case BV_ARRAY:
    result = subst_bvarray(subst, bvarray_term_desc(terms, t));
    break;

  case BV_DIV:
    result = subst_bvdiv(subst, bvdiv_term_desc(terms, t));
    break;

  case BV_REM:
    result = subst_bvrem(subst, bvrem_term_desc(terms, t));
    break;

  case BV_SDIV:
    result = subst_bvsdiv(subst, bvsdiv_term_desc(terms, t));
    break;

  case BV_SREM:
    result = subst_bvsrem(subst, bvsrem_term_desc(terms, t));
    break;

  case BV_SMOD:
    result = subst_bvsmod(subst, bvsmod_term_desc(terms, t));
    break;

  case BV_SHL:
    result = subst_bvshl(subst, bvshl_term_desc(terms, t));
    break;

  case BV_LSHR:
    result = subst_bvlshr(subst, bvlshr_term_desc(terms, t));
    break;

  case BV_ASHR:
    result = subst_bvashr(subst, bvashr_term_desc(terms, t));
    break;

  case BV_EQ_ATOM:
    result = subst_bveq(subst, bveq_atom_desc(terms, t));
    break;

  case BV_GE_ATOM:
    result = subst_bvge(subst, bvge_atom_desc(terms, t));
    break;

  case BV_SGE_ATOM:
    result = subst_bvsge(subst, bvsge_atom_desc(terms, t));
    break;

  case SELECT_TERM:
    result = subst_select(subst, select_term_desc(terms, t));
    break;

  case BIT_TERM:
    result = subst_bit_select(subst, bit_term_desc(terms, t));
    break;

  case POWER_PRODUCT:
    result = subst_pprod(subst, pprod_term_desc(terms, t), term_type(terms, t));
    break;

  case ARITH_POLY:
    result = subst_poly(subst, poly_term_desc(terms, t));
    break;

  case BV64_POLY:
    result = subst_bvpoly64(subst, bvpoly64_term_desc(terms, t));

  case BV_POLY:
    result = subst_bvpoly(subst, bvpoly_term_desc(terms, t));
    break;

  default:
    // error: invalid term_kind or not a composite
    longjmp(subst->env, -2);
    break;
  }

  return result;
}


/*
 * Main substitution function:
 * - if t is atomic and constant return t
 * - if t is a variable, lookup what's mapped to t in subst
 * - otherwise t is composite:
 *   if t is ground, return t
 *   if t is not ground, check the cache; if subst(t) is not
 *   in the cache recursively compute it and store that in the cache.
 */
static term_t get_subst(term_subst_t *subst, term_t t) {
  term_table_t *terms;
  uint32_t polarity;
  term_t result;

  terms = subst->terms;
  assert(good_term(terms, t));

  result = NULL_TERM;
  polarity = polarity_of(t);
  t = unsigned_term(t);

  switch (term_kind(terms, t)) {
  case CONSTANT_TERM:
  case ARITH_CONSTANT:
  case BV64_CONSTANT:
  case BV_CONSTANT:
  case UNINTERPRETED_TERM:
    result = t;
    break;

  case VARIABLE:
    result = get_subst_of_var(subst, t);
    break;

  default:
    /*
     * Composite term
     */
    if (subst_term_is_ground(subst, t)) {
      result = t;
    }  else {
      result = get_cached_subst(subst, t);
      if (result < 0) {
	assert(result == NULL_TERM);
	result = subst_composite(subst, t);
	cache_subst_result(subst, t, result);
      }
    }
    break;
  }

  assert(good_term(terms, result));

  return result ^ polarity;
}



/*
 * Apply the substitution to term t 
 * - t must be a valid term in the subst's term manager
 * - return the resulting term
 *
 * Error codes:
 * - return -1 if the result can't be constructed 
 *   (because of a degree overflow).
 * - return -2 if something else goes wrong
 */
term_t apply_term_subst(term_subst_t *subst, term_t t) {
  term_t result;
  int code;

  code = setjmp(subst->env);
  if (code == 0) {
    result = get_subst(subst, t);
  } else {
    // Error code 
    assert(code == -1 || code == -2);
    result = (term_t) code;

    // Cleanup
    reset_istack(&subst->stack);
    if (subst->rctx != NULL) {
      reset_renaming_ctx(subst->rctx);
    }
  }

  return result;
}