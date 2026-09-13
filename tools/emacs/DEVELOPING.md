<!-- Guides contributors through maintaining and validating Rhodium's Emacs integration. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Developing the Emacs integration

Read the Emacs [README](README.md) for installation, automatic and manual
configuration, behavior, and troubleshooting. This guide owns implementation
changes and focused validation.

## Architecture and ownership

[`rhodium-mode.el`](rhodium-mode.el) is a small adapter around Racket Mode's
`racket-hash-lang-mode`, not an independent major mode. Keep syntax,
indentation, navigation, delimiter behavior, comments, and language services in
the active `#lang` and Racket Mode. This integration owns only `.rhdl`
dispatch, checkout discovery, back-end registration, the `-S` collection path,
and Rhodium-specific presentation after module-language identification.

Do not install dependencies, manage Racket Mode process lifetime, or silently
replace an existing exact-root back end. Preserve Racket Mode's own
longest-prefix selection among registered configurations.

## Implementation map

| Concern | Owner |
|---|---|
| Customization, checkout discovery, registration, dispatch, labeling, and font lock | [`rhodium-mode.el`](rhodium-mode.el) |
| ERT coverage and mocked Racket Mode surface | [`../../tests/emacs/rhodium-mode-test.el`](../../tests/emacs/rhodium-mode-test.el) |
| User installation and troubleshooting | [`README.md`](README.md) |

## Change the integration

1. Keep the load path free of hard-coded checkout locations; discover the root
   from a readable `rhodium/main.rkt` beneath the buffer directory.
2. Preserve the precedence of `rhodium-racket-program`, Racket Mode's
   `racket-program`, and the fallback executable.
3. Register each discovered root at most once and leave exact-root manual
   configurations unchanged.
4. Apply labels and extra faces only after Racket Mode reports one of the two
   supported Rhodium module languages. Remove presentation state when the
   language changes.
5. Add ERT coverage for dispatch order, state transitions, and failure
   diagnostics without requiring a live Racket process.
6. Update [README.md](README.md) when installation, customization, observable
   behavior, or troubleshooting guidance changes.

## Focused validation

Run the ERT suite from the repository root:

```sh
make emacs-test
```

The tests cover file association, checkout discovery, command construction,
one-time registration, exact-root preservation, dispatch order, missing Racket
Mode reporting, module-language labeling, and font-lock installation/removal.
They mock back-end registration and do not start a Racket Mode process or
exercise live editor services. A manual live-editor check is appropriate when
changing behavior that depends on asynchronous language metadata.
