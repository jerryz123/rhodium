#lang rosette
;; SPDX-License-Identifier: Apache-2.0

;; The RTL executor uses packed ports. The spec samples logical members. Layouts
;; come from the elaborated port declarations, in record concatenation order.
(provide pack-pins unpack-pins)
(define (pack-pins layout samples)
  (for/hash ([(pin fields) layout])
    (define words
      (for/list ([f fields])
        (define v (hash-ref samples (car f)))
        (cond [(boolean? v) (if v (bv 1 1) (bv 0 1))]
              [(bv? v) v] [else (bv v (cadr f))])))
    (values pin (foldl (lambda (v acc) (concat acc v)) (car words) (cdr words)))))

(define (unpack-pins layout pins)
  (for*/hash ([(pin fields) layout]
              [field (in-list fields)])
    (define packed (hash-ref pins pin))
    (define lo (apply + (map cadr (cdr (member field fields)))))
    (define hi (+ lo (cadr field)))
    (values (car field) (if (bv? packed) (extract (sub1 hi) lo packed) (bitwise-bit-field packed lo hi)))))
