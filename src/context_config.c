/*
 * OBJECT TO STORE CONTEXT CONFIGURATIONS
 */

#include <stdbool.h>
#include <assert.h>

#include "string_utils.h"
#include "context_config.h"



/*
 * Mapping from strings to context modes (cf. string_utils.h)
 */
static const char * const mode_names[NUM_MODES] = {
  "clean-interrupts",
  "multi-checks",
  "one-shot",
  "push-pop",
};

static const int32_t mode[NUM_MODES] = {
  CTX_MODE_INTERACTIVE,
  CTX_MODE_MULTICHECKS,
  CTX_MODE_ONECHECK,
  CTX_MODE_PUSHPOP,
};


/*
 * Arithmetic fragments
 */
static const char * const fragment_names[NUM_ARITH_FRAGMENTS] = {
  "IDL",
  "LIA",
  "LIRA",
  "LRA",
  "NIA",
  "NIRA",
  "NRA",
  "RDL",
};

static const int32_t fragment[NUM_ARITH_FRAGMENTS] = {
  CTX_CONFIG_ARITH_IDL,
  CTX_CONFIG_ARITH_LIA,
  CTX_CONFIG_ARITH_LIRA,
  CTX_CONFIG_ARITH_LRA,
  CTX_CONFIG_ARITH_NIA,
  CTX_CONFIG_ARITH_NIRA,
  CTX_CONFIG_ARITH_NRA,
  CTX_CONFIG_ARITH_RDL,
};


/*
 * Solver codes
 */
static const char * const solver_code_names[NUM_SOLVER_CODES] = {
  "default",
  "ifw",
  "none",
  "rfw",
  "simplex",
};

static const int32_t solver_code[NUM_SOLVER_CODES] = {
  CTX_CONFIG_DEFAULT,
  CTX_CONFIG_ARITH_IFW,
  CTX_CONFIG_NONE,
  CTX_CONFIG_ARITH_RFW,
  CTX_CONFIG_ARITH_SIMPLEX,
};


/*
 * Descriptor fields (other than "logic")
 */
typedef enum ctx_config_key {
  CTX_CONFIG_KEY_MODE,
  CTX_CONFIG_KEY_ARITH_FRAGMENT,
  CTX_CONFIG_KEY_UF_SOLVER,
  CTX_CONFIG_KEY_ARRAY_SOLVER,
  CTX_CONFIG_KEY_BV_SOLVER,
  CTX_CONFIG_KEY_ARITH_SOLVER,
} ctx_config_key_t;

#define NUM_CONFIG_KEYS (CTX_CONFIG_KEY_ARITH_SOLVER+1)


static const char *const config_key_names[NUM_CONFIG_KEYS] = {
  "arith-fragment",
  "arith-solver",
  "array-solver",
  "bv-solver",
  "mode",
  "uf-solver",
};

static const int32_t config_key[NUM_CONFIG_KEYS] = {
  CTX_CONFIG_KEY_ARITH_FRAGMENT,
  CTX_CONFIG_KEY_ARITH_SOLVER,
  CTX_CONFIG_KEY_ARRAY_SOLVER,
  CTX_CONFIG_KEY_BV_SOLVER,
  CTX_CONFIG_KEY_MODE,
  CTX_CONFIG_KEY_UF_SOLVER,
};





/*
 * CONTEXT SETTING FOR A GIVEN LOGIC CODE
 */

/*
 * Conversion of SMT logic code to a default architecture code
 * -1 means not supported
 *
 * We don't use AUTO_IDL, AUTO_RDL, IFW or RFW here since
 * the Floyd-Warshall solvers don't support all use modes.
 */
static const int32_t logic2arch[NUM_SMT_LOGICS] = {
  -1,                  // AUFLIA
  -1,                  // AUFLIRA
  -1,                  // AUFNIRA
  -1,                  // LRA
  CTX_ARCH_EGFUNBV,    // QF_ABV
  CTX_ARCH_EGFUNBV,    // QF_AUFBV
  CTX_ARCH_EGFUNSPLX,  // QF_AUFLIA
  CTX_ARCH_EGFUN,      // QF_AX
  CTX_ARCH_BV,         // QF_BV
  CTX_ARCH_SPLX,       // QF_IDL
  CTX_ARCH_SPLX,       // QF_LIA
  CTX_ARCH_SPLX,       // QF_LRA
  -1,                  // QF_NIA
  CTX_ARCH_SPLX,       // QF_RDL
  CTX_ARCH_EG,         // QF_UF
  CTX_ARCH_EGBV,       // QF_UFBV[xx]
  CTX_ARCH_EGSPLX,     // QF_UFIDL
  CTX_ARCH_EGSPLX,     // QF_UFLIA
  CTX_ARCH_EGSPLX,     // QF_UFLRA
  -1,                  // QF_UFNRA
  -1,                  // UFNIA
};

/*
 * Specify whether the integer solver should be activated
 */
static const bool logic2iflag[NUM_SMT_LOGICS] = {
  true,   // AUFLIA
  true,   // AUFLIRA
  true,   // AUFNIRA
  false,  // LRA
  false,  // QF_ABV
  false,  // QF_AUFBV
  true,   // QF_AUFLIA
  false,  // QF_AX
  false,  // QF_BV
  false,  // QF_IDL
  true,   // QF_LIA
  false,  // QF_LRA
  true,   // QF_NIA
  false,  // QF_RDL
  false,  // QF_UF
  false,  // QF_UFBV[x]
  false,  // QF_UFIDL
  true,   // QF_UFLIA
  false,  // QF_UFLRA
  false,  // QF_UFNRA
  true,   // UFNIA
};


/*
 * Specify whether quantifier support is needed
 */
static const bool logic2qflag[NUM_SMT_LOGICS] = {
  true,   // AUFLIA
  true,   // AUFLIRA
  true,   // AUFNIRA
  true,   // LRA
  false,  // QF_ABV
  false,  // QF_AUFBV
  false,  // QF_AUFLIA
  false,  // QF_AX
  false,  // QF_BV
  false,  // QF_IDL
  false,  // QF_LIA
  false,  // QF_LRA
  false,  // QF_NIA
  false,  // QF_RDL
  false,  // QF_UF
  false,  // QF_UFBV[x]
  false,  // QF_UFIDL
  false,  // QF_UFLIA
  false,  // QF_UFLRA
  false,  // QF_UFNRA
  true,   // UFNIA
};




/*
 * WHICH ARITHMETIC FRAGMENTS REQUIRE THE DIOPHANTINE SUBSOLVER
 */
static const bool fragment2iflag[NUM_ARITH_FRAGMENTS] = {
  false,  // IDL
  false,  // RDL
  false,  // LRA
  true,   // LIA
  true,   // LIRA
  false,  // NRA
  true,   // NIA
  true,   // NIRA
};


/*
 * Default configuration:
 * - enable PUSH/POP
 * - no logic specified
 * - arith fragment = LIRA
 * - all solvers set to defaults
 */
static const ctx_config_t default_config = {
  CTX_MODE_PUSHPOP,       // mode
  SMT_UNKNOWN,            // logic
  CTX_CONFIG_DEFAULT,     // uf
  CTX_CONFIG_DEFAULT,     // array
  CTX_CONFIG_DEFAULT,     // bv
  CTX_CONFIG_DEFAULT,     // arith
  CTX_CONFIG_ARITH_LIRA,  // fragment
};








/*
 * Initialize config to the default configuration
 */
void init_config_to_defaults(ctx_config_t *config) {
  *config = default_config;
}



/*
 * Set a default configuration to support the given logic
 * - return -1 if the logic name is not recognized
 * - return -2 if we don't support the logic yet
 * - return 0 otherwise 
 *
 * If the function returns 0, the logic field is updated.
 * All other fields are left unchanged.
 */
int32_t config_set_logic(ctx_config_t *config, const char *logic) {
  smt_logic_t code;
  int32_t r;

  code = smt_logic_code(logic);
  if (code == SMT_UNKNOWN) {
    r = -1;
  } else if (logic2arch[code] < 0) {
    r = -2;
  } else {
    config->logic = (smt_logic_t) code;
    r = 0;
  }

  return r;
}


/*
 * Convert value to a solver 
 */
static int32_t set_solver_code(const char *value, solver_code_t *dest) {
  int32_t v, r;

  v = parse_as_keyword(value, solver_code_names, solver_code, NUM_SOLVER_CODES);
  if (v < 0) {
    r = -2;
  } else if (v >= CTX_CONFIG_ARITH_SIMPLEX) {
    r = -3;
  } else {
    assert(v == CTX_CONFIG_DEFAULT || v == CTX_CONFIG_NONE);
    *dest = (solver_code_t) v;
    r = 0;
  }
  
  return r;
}


/*
 * Set an individual field in config
 * - key = field name
 * - value = value for that field
 * 
 * Return code:
 *   -1 if the key is not recognized
 *   -2 if the value is not recognized
 *   -3 if the value is not valid for the key
 *    0 otherwise
 */
int32_t config_set_field(ctx_config_t *config, const char *key, const char *value) {
  int32_t k, v, r;

  r = 0; // return code

  k = parse_as_keyword(key, config_key_names, config_key, NUM_CONFIG_KEYS);
  switch (k) {
  case CTX_CONFIG_KEY_MODE:
    v = parse_as_keyword(value, mode_names, mode, NUM_MODES);
    if (v < 0) {
      r = -2;
    } else {
      config->mode = v;
    }
    break;

  case CTX_CONFIG_KEY_ARITH_FRAGMENT:
    v = parse_as_keyword(value, fragment_names, fragment, NUM_ARITH_FRAGMENTS);
    if (v < 0) {
      r = -2;
    } else {
      assert(0 <= v && v < NUM_ARITH_FRAGMENTS);
      config->arith_fragment = (arith_fragment_t) v;
    }
    break;

  case CTX_CONFIG_KEY_UF_SOLVER:
    r = set_solver_code(value, &config->uf_config);
    break;

  case CTX_CONFIG_KEY_ARRAY_SOLVER:
    r = set_solver_code(value, &config->array_config);
    break;

  case CTX_CONFIG_KEY_BV_SOLVER:
    r = set_solver_code(value, &config->bv_config);
    break;

  case CTX_CONFIG_KEY_ARITH_SOLVER:
    v = parse_as_keyword(value, solver_code_names, solver_code, NUM_SOLVER_CODES);
    if (v < 0) {
      r = -2;
    } else {
      assert(0 <= v && v <= NUM_SOLVER_CODES);
      config->arith_config = v;
    }
    break;

  default:
    assert(k == -1);
    r = -1;
    break;
  }

  return r;
}




/*
 * Auxiliary functions to build architecture codes incrementally
 * - each function takes an integer a: a is either a valid architecture
 *   code or -1 
 * - then the function adds a new solver component to a: this results
 *   in either a new valid code or -1 if the new component is not compatible with a.
 *
 * Important: we assume that the components are added in the following 
 * order: egraph, array solver, bitvector solver, arithmetic solver
 */
static inline int32_t arch_add_egraph(int32_t a) {
  if (a == CTX_ARCH_NOSOLVERS) {
    a = CTX_ARCH_EG;
  } else {
    a = -1;
  }
  return a;
}

static int32_t arch_add_array(int32_t a) {
  if (a == CTX_ARCH_EG || a == CTX_ARCH_NOSOLVERS) {
    a = CTX_ARCH_EGFUN; // array requires egraph to we add both implicitly
  } else {
    a = -1;
  }
  return a;
}

static int32_t arch_add_bv(int32_t a) {
  switch (a) {
  case CTX_ARCH_NOSOLVERS:
    a = CTX_ARCH_BV;
    break;

  case CTX_ARCH_EG:
    a = CTX_ARCH_EGBV;
    break;

  case CTX_ARCH_EGFUN:
    a = CTX_ARCH_EGFUNBV;
    break;

  default:
    a = -1;
    break;
  }

  return a;
}

// add the simplex solver 
static int32_t arch_add_simplex(int32_t a) {
  switch (a) {
  case CTX_ARCH_NOSOLVERS:
    a = CTX_ARCH_SPLX;
    break;

  case CTX_ARCH_EG:
    a = CTX_ARCH_EGSPLX;
    break;

  case CTX_ARCH_EGFUN:
    a = CTX_ARCH_EGFUNSPLX;
    break;

  case CTX_ARCH_EGBV:
    a = CTX_ARCH_EGSPLXBV;
    break;
    
  case CTX_ARCH_EGFUNBV:
    a = CTX_ARCH_EGFUNSPLXBV;
    break;

  default:
    a = -1;
    break;
  }

  return a;
}

// add a Floyd-Warshall solver
static int32_t arch_add_ifw(int32_t a) {
  if (a == CTX_ARCH_NOSOLVERS) {
    a = CTX_ARCH_IFW;
  } else {
    a = -1;
  }
  return a;
}

static int32_t arch_add_rfw(int32_t a) {
  if (a == CTX_ARCH_NOSOLVERS) {
    a = CTX_ARCH_RFW;
  } else {
    a = -1;
  }
  return a;
}


// add solver identified by code c to a
static int32_t arch_add_arith(int32_t a, solver_code_t c) {
  switch (c) {
  case CTX_CONFIG_NONE:  // no arithmetic solver
    break;

  case CTX_CONFIG_DEFAULT:  // simplex is the default
  case CTX_CONFIG_ARITH_SIMPLEX:
    a = arch_add_simplex(a);
    break;

  case CTX_CONFIG_ARITH_IFW:
    a = arch_add_ifw(a);
    break;

  case CTX_CONFIG_ARITH_RFW:
    a = arch_add_rfw(a);
    break;
  }
  return a;
}


/*
 * Check whether the architecture code a is compatible with mode
 * - current restriction: IFW and RFW don't support PUSH/POP or MULTIPLE CHECKS
 */
static bool arch_supports_mode(context_arch_t a, context_mode_t mode) {
  return (a != CTX_ARCH_IFW && a != CTX_ARCH_RFW) || mode == CTX_MODE_ONECHECK;
}


/*
 * Check whether config is valid (and supported by this version of Yices)
 * and convert it to a tuple (arch, mode, iflag, qflag)
 * - arch = architecture code as defined in context.h
 * - mode = one of the context's modes
 * - iflag = true if the integer solver (in simplex) is required
 * - qflag = true if support for quantifiers is required
 *
 * Return code:
 *   0 if the config is valid and supported
 *  -1 if the config is invalid
 *  -2 if the config is valid but not currently supported
 *  -3 if the solver combination is valid but does not support the specified mode
 */
int32_t decode_config(const ctx_config_t *config, context_arch_t *arch, 
		      context_mode_t *mode, bool *iflag, bool *qflag) {

  smt_logic_t logic_code;
  int32_t a, r;

  r = 0; // default return code

  logic_code = config->logic;
  if (logic_code != SMT_UNKNOWN) {
    /*
     * The intended logic is specified
     */
    assert(0 <= logic_code && logic_code < NUM_SMT_LOGICS);
    a = logic2arch[logic_code];
    if (a < 0) {
      // not supported
      r = -2;
    } else {
      // good configuration
      *arch = (context_arch_t) a;
      *iflag = logic2iflag[logic_code];
      *qflag = logic2qflag[logic_code];
      *mode = config->mode;
    }

  } else {
    /*
     * No logic specified.
     */
    a = CTX_ARCH_NOSOLVERS;
    if (config->uf_config == CTX_CONFIG_DEFAULT) {
      a = arch_add_egraph(a);
    }
    if (config->array_config == CTX_CONFIG_DEFAULT) {
      a = arch_add_array(a);
    }
    if (config->array_config == CTX_CONFIG_DEFAULT) {
      a = arch_add_bv(a);
    }
    a = arch_add_arith(a, config->arith_config);

    // a is either -1 or an architecture code
    if (a < 0) {
      r = -1; // invalid combination of solvers
    } else if (arch_supports_mode(a, config->mode)) {
      // good configuration
      *arch = (context_arch_t) a;      
      *iflag = fragment2iflag[config->arith_fragment];
      *qflag = false;
      *mode = config->mode;
    } else {
      // mode is not supported by the solvers
      r = -2;
    }
  }

  return r;
}
