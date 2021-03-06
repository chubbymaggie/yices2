(set-info :source |fuzzsmt|)
(set-info :smt-lib-version 2.0)
(set-info :category "random")
(set-info :status unknown)
(set-logic QF_LRA)
(declare-fun v0 () Real)
(assert (let ((e1 1))
(let ((e2 (! (- v0 v0) :named term2)))
(let ((e3 (! (/ e1 (- e1)) :named term3)))
(let ((e4 (! (> v0 e2) :named term4)))
(let ((e5 (! (= e3 e2) :named term5)))
(let ((e6 (! (ite e4 e2 e2) :named term6)))
(let ((e7 (! (ite e5 e3 e3) :named term7)))
(let ((e8 (! (ite e4 v0 e7) :named term8)))
(let ((e9 (! (< e8 e2) :named term9)))
(let ((e10 (! (< v0 e3) :named term10)))
(let ((e11 (! (> e3 e6) :named term11)))
(let ((e12 (! (>= e2 e7) :named term12)))
(let ((e13 (! (not e5) :named term13)))
(let ((e14 (! (or e13 e4) :named term14)))
(let ((e15 (! (=> e12 e14) :named term15)))
(let ((e16 (! (and e15 e10) :named term16)))
(let ((e17 (! (ite e11 e11 e16) :named term17)))
(let ((e18 (! (and e17 e9) :named term18)))
e18
)))))))))))))))))))

(check-sat)
(set-option :regular-output-channel "/dev/null")
(get-model)
(get-value (term2))
(get-value (term3))
(get-value (term4))
(get-value (term5))
(get-value (term6))
(get-value (term7))
(get-value (term8))
(get-value (term9))
(get-value (term10))
(get-value (term11))
(get-value (term12))
(get-value (term13))
(get-value (term14))
(get-value (term15))
(get-value (term16))
(get-value (term17))
(get-value (term18))
(get-info :all-statistics)
