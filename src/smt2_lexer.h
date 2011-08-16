/*
 * Lexer for the SMT-LIB2 Language
 * 
 * This should be enough to parse scripts in the SMT-LIB 2.0
 * language. We do not support the theory declaration and
 * the logic declaration parts of the language.
 */

#ifndef __SMT2_LEXER_H
#define __SMT2_LEXER_H

#include <stdint.h>

#include "lexer.h"
#include "smt_logic_codes.h"

/*
 * Tokens
 */
enum smt2_token {
  // open/close par
  SMT2_TK_LP,
  SMT2_TK_RP,

  // end of stream
  SMT2_TK_EOS,

  // atomic tokens
  SMT2_TK_NUMERAL,
  SMT2_TK_DECIMAL,
  SMT2_TK_HEXADECIMAL,
  SMT2_TK_BINARY,
  SMT2_TK_STRING,
  SMT2_TK_SYMBOL,
  SMT2_TK_KEYWORD,

  // Reserved words
  SMT2_TK_PAR,
  SMT2_TK_NUM,
  SMT2_TK_DEC,
  SMT2_TK_STR,
  SMT2_TK_UNDERSCORE,
  SMT2_TK_BANG,
  SMT2_TK_AS,
  SMT2_TK_LET,
  SMT2_TK_EXISTS,
  SMT2_TK_FORALL,

  // Commands
  SMT2_TK_ASSERT,
  SMT2_TK_CHECK_SAT,
  SMT2_TK_DECLARE_SORT,
  SMT2_TK_DECLARE_FUN,
  SMT2_TK_DEFINE_SORT,
  SMT2_TK_DEFINE_FUN,
  SMT2_TK_EXIT,
  SMT2_TK_GET_ASSERTIONS,
  SMT2_TK_GET_ASSIGNMENT,
  SMT2_TK_GET_INFO,
  SMT2_TK_GET_OPTION,
  SMT2_TK_GET_PROOF,
  SMT2_TK_GET_UNSAT_CORE,
  SMT2_TK_GET_VALUE,
  SMT2_TK_POP,
  SMT2_TK_PUSH,
  SMT2_TK_SET_LOGIC,
  SMT2_TK_SET_INFO,
  SMT2_TK_SET_OPTION,

  // Errors
  SMT2_TK_INVALID_STRING,
  SMT2_TK_INVALID_NUMERAL,
  SMT2_TK_INVALID_DECIMAL,
  SMT2_TK_INVALID_HEXADECIMAL,
  SMT2_TK_INVALID_BINARY,
  SMT2_TK_INVALID_SYMBOL,
  SMT2_TK_INVALID_KEYWORD,
  SMT2_TK_ERROR,
};

typedef enum smt2_token smt2_token_t;

#define NUM_SMT2_TOKENS (SMT2_TK_ERROR+1)

/*
 * Predefined keywords
 */
enum smt2_keyword {
  // Predefined keywords for (set-option ...)
  SMT2_KW_PRINT_SUCCESS,
  SMT2_KW_EXPAND_DEFINITIONS,
  SMT2_KW_INTERACTIVE_MODE,
  SMT2_KW_PRODUCE_PROOFS,
  SMT2_KW_PRODUCE_UNSAT_CORES,
  SMT2_KW_PRODUCE_MODELS,
  SMT2_KW_PRODUCE_ASSIGNMENTS,
  SMT2_KW_REGULAR_OUTPUT,
  SMT2_KW_DIAGNOSTIC_OUTPUT,
  SMT2_KW_RANDOM_SEED,
  SMT2_KW_VERBOSITY,

  // Predefined keywords for (set-info ...)
  SMT2_KW_ERROR_BEHAVIOR,
  SMT2_KW_NAME,
  SMT2_KW_AUTHORS,
  SMT2_KW_VERSION,
  SMT2_KW_STATUS,
  SMT2_KW_REASON_UNKNOWN,
  SMT2_KW_ALL_STATISTICS,

  // Attribute names for terms
  SMT2_KW_NAMED,
  SMT2_KW_PATTERN,   // not quite an official name yet

  // Anything other keyword
  SMT2_KW_UNKNOWN,
};

typedef enum smt2_keyword smt2_keyword_t;

#define NUM_SMT2_KEYWORDS (SMT2_KW_UNKNOWN+1)


/*
 * Predefined symbols
 */
enum smt2_symbol {
  // Core theory
  SMT2_SYM_BOOL,
  SMT2_SYM_TRUE,
  SMT2_SYM_FALSE,
  SMT2_SYM_NOT,
  SMT2_SYM_IMPLIES,
  SMT2_SYM_AND,
  SMT2_SYM_OR,
  SMT2_SYM_XOR,
  SMT2_SYM_EQ,
  SMT2_SYM_DISTINCT,
  SMT2_SYM_ITE,

  // Array theory
  SMT2_SYM_ARRAY,
  SMT2_SYM_SELECT,
  SMT2_SYM_STORE,

  // Arithmetic symbols
  SMT2_SYM_INT,
  SMT2_SYM_REAL,
  SMT2_SYM_MINUS,
  SMT2_SYM_PLUS,
  SMT2_SYM_TIMES,
  SMT2_SYM_DIVIDES,
  SMT2_SYM_LE,
  SMT2_SYM_LT,
  SMT2_SYM_GE,
  SMT2_SYM_GT,
  SMT2_SYM_DIV,
  SMT2_SYM_MOD,
  SMT2_SYM_ABS,
  SMT2_SYM_TO_REAL,
  SMT2_SYM_TO_INT,
  SMT2_SYM_IS_INT,
  SMT2_SYM_DIVISIBLE,

  // Special symbols used in the BV theory: (_ bv<numeral> <numeral>)
  SMT2_SYM_BV_CONSTANT,

  // Bit-vector symbols
  SMT2_SYM_BITVEC,
  SMT2_SYM_CONCAT,
  SMT2_SYM_EXTRACT,
  SMT2_SYM_REPEAT,
  SMT2_SYM_BVCOMP,
  SMT2_SYM_BVREDOR,    // In SMT1.2 but not in SMT2.0 (keep it defined but inactive)
  SMT2_SYM_BVREDAND,   // In SMT1.2 but not in SMT2.0 (same thing)

  SMT2_SYM_BVNOT,
  SMT2_SYM_BVAND,
  SMT2_SYM_BVOR,
  SMT2_SYM_BVNAND,
  SMT2_SYM_BVNOR,
  SMT2_SYM_BVXOR,
  SMT2_SYM_BVXNOR,

  SMT2_SYM_BVNEG,
  SMT2_SYM_BVADD,
  SMT2_SYM_BVSUB,
  SMT2_SYM_BVMUL,
  SMT2_SYM_BVUDIV,
  SMT2_SYM_BVUREM,
  SMT2_SYM_BVSDIV,
  SMT2_SYM_BVSREM,
  SMT2_SYM_BVSMOD,

  SMT2_SYM_BVSHL,
  SMT2_SYM_BVLSHR,
  SMT2_SYM_BVASHR,
  SMT2_SYM_ZERO_EXTEND,
  SMT2_SYM_SIGN_EXTEND,
  SMT2_SYM_ROTATE_LEFT,
  SMT2_SYM_ROTATE_RIGHT,

  SMT2_SYM_BVULT,
  SMT2_SYM_BVULE,
  SMT2_SYM_BVUGT,
  SMT2_SYM_BVUGE,
  SMT2_SYM_BVSLT,
  SMT2_SYM_BVSLE,
  SMT2_SYM_BVSGT,
  SMT2_SYM_BVSGE,

  // Errors
  SMT2_SYM_INVALID_BV_CONSTANT,

  // Not predefined
  SMT2_SYM_UNKNOWN,
};

typedef enum smt2_symbol smt2_symbol_t;

#define NUM_SMT2_SYMBOLS (SMT2_SYM_UNKNOWN+1)


/*
 * NOTE: The following are indexed symbols
 * 
 * BitVec:  (_ BitVec n): bitvectors of n bits, n >= 1
 *
 * extract: (_ extract i j): (_ BitVec m) -> (_ BitVec n)
 *    where 0 <= j <= i < m and n = i-j+1
 *    extract bits[j .. i] of u
 *
 * repeat:  (_ repeat i): (_ BitVec n) -> (_ BitVec n*i)
 *    where i>=1: concatenate u, i times with itself
 *
 * zero_extend: (_ zero_extend i) where i >= 0
 *   append i '0' on the left-hand side (high-order bits)
 *
 * sign_extend: (_ signe_extend i) where i >= 0
 *
 * rotate_left: (_ rotate_left i) where i >= 0
 * rotate_right: (_ rotate_right i) where i >= 0
 */
 
/*
 * Lexer initialization:
 * - init_smt2_file_lexer returns -1 if the file can't be opened
 *   or 0 otherwise.
 */
extern int32_t init_smt2_file_lexer(lexer_t *lex, char *filename);

extern void init_smt2_stream_lexer(lexer_t *lex, FILE *f, char *name);

static inline void init_smt2_stdin_lexer(lexer_t *lex) {
  init_smt2_stream_lexer(lex, stdin, "stdin");
}

extern void init_smt2_string_lexer(lexer_t *lex, char *data, char *name);


/*
 * Read next token and return its type
 * - update lex->token (set it to tk)
 * - update lex->tk_pos, tk_line, tk_pos (to the start of token
 *   in the input stream)
 * - for any token other than '(', ')', EOF, or SMT2_TK_ERROR, the 
 *   token value is stored in lex->buffer (as a character string).
 */
extern smt2_token_t next_smt2_token(lexer_t *lex);


/*
 * Conversion of tokens/symbols/keywords to strings
 */
extern const char *smt2_token_to_string(smt2_token_t tk);
extern const char *smt2_keyword_to_string(smt2_keyword_t kw);
extern const char *smt2_symbol_to_string(smt2_symbol_t sym);


/*
 * Map string s to a keyword id:
 * - n = length of the string
 * - return SMT2_KW_UNKNOWN if s is not in the keyword table
 */
extern smt2_keyword_t smt2_string_to_keyword(const char *s, uint32_t n);


/*
 * Select built-in symbols for logic:
 * - this determines which symbols are active
 * - the symbols in the core theory are always active
 */
extern void smt2_lexer_activate_logic(smt_logic_t logic);


/*
 * Check whether a symbol is currently active
 */
extern bool smt2_symbol_is_active(smt2_symbol_t sym);


/*
 * Map string s to a symbol id
 * - n = length of the string
 * - return SMT2_SYM_UNKNOWN if s is not a predefined symbol,
 *   or if it's not active.
 *
 * Special treatment for bitvector constants, when the bit-vector
 * theory is active:
 * - if the string is of the form 'bv<numeral>' then the 
 *   returned id is SMT2_SYM_BV_CONSTANT
 * - if the string starts with 'bv' but the rest is not <numeral>, then
 *   the returned if is SMT2_SYM_INVALID_BV_CONSTANT
 */
extern smt2_symbol_t smt2_string_to_symbol(const char *s, uint32_t n);




#endif /* __SMT2_LEXER_H */