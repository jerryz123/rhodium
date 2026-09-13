;;; rhodium-mode-test.el --- Tests for Rhodium's Emacs entry point -*- lexical-binding: t; -*-
;; SPDX-License-Identifier: Apache-2.0

(require 'ert)
(require 'cl-lib)
(require 'rhodium-mode)

(defvar racket-back-end-configurations)

(defconst rhodium-test-checkout-root
  (file-name-as-directory
   (expand-file-name "../.." (file-name-directory load-file-name))))

(ert-deftest rhodium-mode-registers-rhodium-extension ()
  (should (eq (cdr (assoc "\\.rhdl\\'" auto-mode-alist)) 'rhodium-mode)))

(ert-deftest rhodium-mode-finds-checkout-root ()
  (should
   (equal (rhodium--checkout-root
           (expand-file-name "examples/lop" rhodium-test-checkout-root))
          rhodium-test-checkout-root)))

(ert-deftest rhodium-mode-builds-project-racket-command ()
  (let ((rhodium-racket-program '("racket" "-j")))
    (should
     (equal (rhodium--racket-command rhodium-test-checkout-root)
            (list "racket" "-j" "-S"
                  (directory-file-name rhodium-test-checkout-root))))))

(ert-deftest rhodium-mode-configures-checkout-once ()
  (let ((racket-back-end-configurations nil)
        (rhodium--configured-roots nil)
        (rhodium-racket-program "racket")
        calls)
    (cl-letf (((symbol-function 'rhodium--checkout-root)
               (lambda (&optional _directory) rhodium-test-checkout-root))
              ((symbol-function 'racket-add-back-end)
               (lambda (root &rest options)
                 (push (cons root options) calls))))
      (rhodium--ensure-back-end)
      (rhodium--ensure-back-end))
    (should (= (length calls) 1))
    (should
     (equal (car calls)
            (list rhodium-test-checkout-root
                  :racket-program
                  (list "racket" "-S"
                        (directory-file-name rhodium-test-checkout-root)))))))

(ert-deftest rhodium-mode-respects-explicit-checkout-back-end ()
  (let ((racket-back-end-configurations
         (list (list :directory rhodium-test-checkout-root
                     :racket-program '("custom-racket"))))
        (rhodium--configured-roots nil)
        called)
    (cl-letf (((symbol-function 'rhodium--checkout-root)
               (lambda (&optional _directory) rhodium-test-checkout-root))
              ((symbol-function 'racket-add-back-end)
               (lambda (&rest _arguments) (setq called t))))
      (rhodium--ensure-back-end))
    (should-not called)))

(ert-deftest rhodium-mode-dispatches-without-changing-back-end-order ()
  (let (events)
    (cl-letf (((symbol-function 'rhodium--load-racket-mode)
               (lambda () (push 'load events)))
              ((symbol-function 'rhodium--ensure-back-end)
               (lambda () (push 'configure events)))
              ((symbol-function 'racket-hash-lang-mode)
               (lambda () (push 'mode events))))
      (rhodium-mode))
    (should (equal (reverse events) '(load configure mode)))))

(ert-deftest rhodium-mode-reports-missing-racket-mode ()
  (cl-letf (((symbol-function 'require)
             (lambda (&rest _arguments) nil)))
    (should-error (rhodium--load-racket-mode) :type 'user-error)))

(ert-deftest rhodium-mode-labels-only-rhodium-module-languages ()
  (with-temp-buffer
    (setq-local racket-hash-lang-mode-lighter "#lang⇉")
    (rhodium--language-setup "(lib rhodium/language.rhm)")
    (should (equal racket-hash-lang-mode-lighter "Rhodium⇉")))
  (with-temp-buffer
    (setq-local racket-hash-lang-mode-lighter "#lang⇉")
    (rhodium--language-setup "(lib rhodium/base/language.rhm)")
    (should (equal racket-hash-lang-mode-lighter "Rhodium⇉")))
  (with-temp-buffer
    (setq-local racket-hash-lang-mode-lighter "#lang⇉")
    (rhodium--language-setup "(lib rhombus/main.rhm)")
    (should (equal racket-hash-lang-mode-lighter "#lang⇉"))))

(ert-deftest rhodium-mode-highlights-rhodium-syntax-and-types ()
  (with-temp-buffer
    (prog-mode)
    (insert "circuit Example():\n"
            "  input value: Bits(1)\n"
            "  assert(value, \"valid\")\n"
            "def my_circuit = Example()\n")
    (font-lock-mode 1)
    (setq-local racket-hash-lang-mode-lighter "#lang")
    (rhodium--language-setup "(lib rhodium/language.rhm)")
    (font-lock-ensure)
    (dolist (word '("circuit" "input" "assert"))
      (goto-char (point-min))
      (search-forward word)
      (should (eq (get-text-property (- (point) (length word)) 'face)
                  'font-lock-keyword-face)))
    (goto-char (point-min))
    (search-forward "Bits")
    (should (eq (get-text-property (- (point) 4) 'face)
                'font-lock-type-face))
    (goto-char (point-min))
    (search-forward "my_circuit")
    (should-not (get-text-property (- (point) (length "my_circuit")) 'face))
    (rhodium--language-setup "(lib rhombus/main.rhm)")
    (font-lock-ensure)
    (goto-char (point-min))
    (search-forward "circuit")
    (should-not (get-text-property (- (point) (length "circuit")) 'face))))

;;; rhodium-mode-test.el ends here
