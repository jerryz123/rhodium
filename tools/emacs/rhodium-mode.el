;;; rhodium-mode.el --- Project-aware Emacs entry point for Rhodium -*- lexical-binding: t; -*-
;; SPDX-License-Identifier: Apache-2.0

;; Package-Requires: ((emacs "25.1") (racket-mode "1.0"))
;; Version: 0.1.0
;; Keywords: languages, hardware
;; URL: https://github.com/jerryz123/rhdl

;;; Commentary:

;; Rhodium uses Racket Mode's `racket-hash-lang-mode' for language-provided
;; coloring, indentation, navigation, comments, and REPL integration.  This
;; file adds a discoverable `rhodium-mode' command and makes uninstalled Rhodium
;; checkouts visible to the Racket Mode back end through Racket's -S option.

;;; Code:

(require 'cl-lib)
(require 'subr-x)

(declare-function racket-add-back-end "racket-back-end" (directory &rest plist))
(declare-function racket-hash-lang-mode "racket-hash-lang" ())

(defvar racket-back-end-configurations)
(defvar racket-hash-lang-mode-lighter)
(defvar racket-program)

(defgroup rhodium nil
  "Editing support for the Rhodium hardware description language."
  :group 'languages)

(defcustom rhodium-racket-program nil
  "Racket command used for Rhodium checkout back ends.

When nil, inherit `racket-program'.  Like `racket-program', the value may be
either an executable string or a list containing an executable and arguments."
  :type '(choice (const :tag "Inherit Racket Mode" nil)
                 string
                 (repeat string))
  :group 'rhodium)

(defcustom rhodium-auto-configure-back-end t
  "Whether `rhodium-mode' should configure Racket Mode for an Rhodium checkout.

The automatic configuration adds the checkout root with Racket's -S option.
It does not replace an exact Racket Mode back-end configuration already
registered for that root."
  :type 'boolean
  :group 'rhodium)

(defvar rhodium--configured-roots nil
  "Checkout roots considered for automatic back-end configuration.")

(defconst rhodium--font-lock-keywords
  `((,(regexp-opt
       '("assert" "bundle" "case" "circuit" "dpi_import" "dpi_reg"
         "elaborate" "elsewhen" "hardware_enum" "input" "inst"
         "interface" "mem" "mux_lookup" "otherwise"
         "output" "record" "refines" "reg" "supports" "switch"
         "sync_circuit" "sync_mem" "when" "wire")
       'symbols)
     . font-lock-keyword-face)
    (,(regexp-opt '("Bits" "Bool" "Clock" "Mask" "MaybeOneHot" "OneHot" "Reset" "SInt" "Vec")
                  'symbols)
     . font-lock-type-face))
  "Additional font-lock rules for Rhodium-specific syntax and types.")

(defvar-local rhodium--font-lock-installed nil
  "Whether Rhodium-specific font-lock rules are installed in this buffer.")

(defun rhodium--checkout-root (&optional directory)
  "Return the Rhodium checkout containing DIRECTORY, or nil.

An Rhodium checkout is identified by its `rhodium/main.rkt' reader shim."
  (when-let ((root
              (locate-dominating-file
               (or directory default-directory)
               (lambda (candidate)
                 (file-readable-p
                  (expand-file-name "rhodium/main.rkt" candidate))))))
    (file-name-as-directory (expand-file-name root))))

(defun rhodium--racket-command (root)
  "Return the Racket command for an Rhodium checkout at ROOT."
  (let* ((configured (or rhodium-racket-program
                         (and (boundp 'racket-program) racket-program)
                         "racket"))
         (command (if (listp configured)
                      (copy-sequence configured)
                    (list configured)))
         (collection-root (or (file-remote-p root 'localname) root)))
    (append command
            (list "-S" (directory-file-name collection-root)))))

(defun rhodium--existing-back-end-p (root)
  "Return non-nil when Racket Mode already has a back end for exactly ROOT."
  (and (boundp 'racket-back-end-configurations)
       (cl-find-if
        (lambda (configuration)
          (string-equal
           (file-name-as-directory (plist-get configuration :directory))
           (file-name-as-directory root)))
        racket-back-end-configurations)))

(defun rhodium--ensure-back-end ()
  "Configure a project-specific Racket Mode back end when appropriate."
  (when rhodium-auto-configure-back-end
    (when-let ((root (rhodium--checkout-root)))
      (unless (member root rhodium--configured-roots)
        (unless (rhodium--existing-back-end-p root)
          (racket-add-back-end root
                               :racket-program (rhodium--racket-command root)))
        (push root rhodium--configured-roots)))))

(defun rhodium--module-language-p (module-language)
  "Return non-nil when MODULE-LANGUAGE names an Rhodium language profile."
  (member module-language
          '("(lib rhodium/language.rhm)"
            "(lib rhodium/base/language.rhm)")))

(defun rhodium--configure-font-lock (enable)
  "Install Rhodium font-lock rules when ENABLE is non-nil, otherwise remove them."
  (cond
   ((and enable (not rhodium--font-lock-installed))
    (font-lock-add-keywords nil rhodium--font-lock-keywords 'append)
    (setq-local rhodium--font-lock-installed t)
    (font-lock-flush))
   ((and (not enable) rhodium--font-lock-installed)
    (font-lock-remove-keywords nil rhodium--font-lock-keywords)
    (setq-local rhodium--font-lock-installed nil)
    (font-lock-flush))))

(defun rhodium--language-setup (module-language)
  "Apply Rhodium presentation after loading MODULE-LANGUAGE metadata."
  (let ((rhodium-language-p (rhodium--module-language-p module-language)))
    (rhodium--configure-font-lock rhodium-language-p)
    (when rhodium-language-p
      (setq-local racket-hash-lang-mode-lighter
                  (replace-regexp-in-string
                   "\\`#lang" "Rhodium" racket-hash-lang-mode-lighter)))))

(with-eval-after-load 'racket-hash-lang
  (add-hook 'racket-hash-lang-module-language-hook
            #'rhodium--language-setup))

(defun rhodium--load-racket-mode ()
  "Load Racket Mode or report the missing editor dependency."
  (unless (require 'racket-mode nil t)
    (user-error "Rhodium mode requires the Emacs package racket-mode")))

;;;###autoload
(defun rhodium-mode ()
  "Edit an Rhodium buffer using Racket Mode's hash-language back end.

The buffer's actual `major-mode' remains `racket-hash-lang-mode' because
Racket Mode currently relies on that exact identity for some integrations."
  (interactive)
  (rhodium--load-racket-mode)
  (rhodium--ensure-back-end)
  (racket-hash-lang-mode))

;;;###autoload
(add-to-list 'auto-mode-alist '("\\.rhdl\\'" . rhodium-mode))

(provide 'rhodium-mode)

;;; rhodium-mode.el ends here
