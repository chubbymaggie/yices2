(set-logic QF_LIA)
(declare-fun x () Int)
(assert (>= (div x (- 4)) 2))
(check-sat)
(get-model)
(get-value (x (div x (- 4)) (mod x (- 4))))