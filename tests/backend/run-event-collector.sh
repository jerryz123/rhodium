#!/usr/bin/env bash
# Runs standalone collector contracts and parses the resulting trace bundle.
set -euo pipefail
repo_dir="$(cd "$(dirname "$0")/../.." && pwd)"
collector_tmp_dir="$(mktemp -d /tmp/rhodium-event-collector.XXXXXX)"
trap 'rm -rf "$collector_tmp_dir"' EXIT
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
  "$repo_dir/tests/backend/event-collector-test.cpp" \
  "$repo_dir/rhodium/event/runtime/rhodium_event.cc" \
  -o "$collector_tmp_dir/event-collector-test"
"$collector_tmp_dir/event-collector-test" > "$collector_tmp_dir/trace.json"
"$repo_dir/tools/run-racket.sh" -e '
  (require json)
  (define trace (call-with-input-file (vector-ref (current-command-line-arguments) 0) read-json))
  (define manifest (hash-ref trace (quote manifest)))
  (define occurrences (hash-ref trace (quote occurrences)))
  (unless (and (equal? (hash-ref trace (quote format)) "rhodium-event-trace")
               (= (hash-ref trace (quote version)) 1)
               (equal? (hash-ref manifest (quote format)) "rhodium-event-graph")
               (equal? (hash-ref occurrences (quote format)) "rhodium-event-occurrences")
               (= (length (hash-ref manifest (quote sites))) 2)
               (= (length (hash-ref occurrences (quote nodes))) 3)
               (= (length (hash-ref occurrences (quote edges))) 2)
               (equal? (hash-ref (car (hash-ref occurrences (quote nodes))) (quote sequence)) "9007199254740993"))
    (error (quote event-trace) "invalid bundled export"))
' "$collector_tmp_dir/trace.json"
