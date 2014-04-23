/*
 * RECORD TO STORE SEARCH OPTIONS/SETTINGS
 */

#ifndef __SEARCH_PARAMETERS_H
#define __SEARCH_PARAMETERS_H


#include <stdbool.h>
#include <stdint.h>

#include "yices_types.h"
#include "yices_locks.h"


/*
 * Possible branching heuristics:
 * - determine whether to assign the decision literal to true or false
 */
typedef enum {
  BRANCHING_DEFAULT,  // use internal smt_core cache
  BRANCHING_NEGATIVE, // branch l := false
  BRANCHING_POSITIVE, // branch l := true
  BRANCHING_THEORY,   // defer to the theory solver
  BRANCHING_TH_NEG,   // defer to theory solver for atoms, branch l := false otherwise
  BRANCHING_TH_POS,   // defer to theory solver for atoms, branch l := true otherwise
} branch_t;

#define NUM_BRANCHING_MODES 6


struct param_s {

  yices_lock_t lock;

  /*
   * Restart heuristic: similar to PICOSAT or MINISAT
   *
   * If fast_restart is true: PICOSAT-style heuristic
   * - inner restarts based on c_threshold
   * - outer restarts based on d_threshold
   *
   * If fast_restart is false: MINISAT-style restarts
   * - c_threshold and c_factor are used
   * - d_threshold and d_threshold are ignored
   * - to get periodic restart set c_factor = 1.0
   */
  bool     fast_restart;
  uint32_t c_threshold;     // initial value of c_threshold
  uint32_t d_threshold;     // initial value of d_threshold
  double   c_factor;        // increase factor for next c_threshold
  double   d_factor;        // increase factor for next d_threshold

  /*
   * Clause-deletion heuristic
   * - initial reduce_threshold is max(r_threshold, num_prob_clauses * r_fraction)
   * - increase by r_factor on every reduce
   */
  uint32_t r_threshold;
  double   r_fraction;
  double   r_factor;

  /*
   * SMT Core parameters:
   * - randomness and var_decay are used by the branching heuristic
   *   the default branching mode uses the cached polarity in smt_core.
   * - clause_decay influence clause deletion
   * - random seed
   */
  double   var_decay;       // decay factor for variable activity
  float    randomness;      // probability of a random pick in select_unassigned_literal
  uint32_t random_seed;
  branch_t branching;       // branching heuristic
  float    clause_decay;    // decay factor for learned-clause activity
};



/************************
 *  PARAMETER RECORDS   *
 ***********************/

/*
 * Initialize params with default values
 */
extern void init_params_to_defaults(param_t *parameters);



/*
 * Set a field in the parameter record
 * - key = field name
 * - value = value for that field
 *
 * Return code:
 *  -1 if the key is not recognized
 *  -2 if the value is not recognized
 *  -3 if the value is not valid for the key
 *   0 otherwise
 */
extern int32_t params_set_field(param_t *parameters, const char *key, const char *value);




#endif /* __SEARCH_PARAMETERS_H */
