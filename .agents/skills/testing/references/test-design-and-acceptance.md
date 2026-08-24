# Test Design and Acceptance

Read this reference when planning, writing, or reviewing unit/regression tests. Keep the work tied to this repository's
actual framework, contracts, fixtures, and commands; do not substitute generic testing theory.

## Establish evidence and risk

1. Locate the behavior source: the user requirement, public API, protobuf/schema, configuration or protocol contract,
   current caller, or a reproduced defect. Inspect the production entry point, its material side effects, and the nearest
   healthy tests before choosing cases.
2. Define the system under test, what must work, what is most likely to fail, what has the highest impact, and which
   result/state/side effect is externally observable. Protect those risks first.
3. For every case, name the realistic production break it should catch — a bug, not a decision. If no observable bug
   would make it fail, remove it or redesign it; do not add tests for counts, trivial forwarding, static definitions, or
   deleted code. If only an intentional decision change (a constant's value, exact message wording, private layout)
   would fail the case, it is a change detector; test the consumer-visible behavior that depends on the decision
   instead.
4. If the scope, interface, field, environment, prerequisite, or root cause is not verified, record the assumption and
   risk gap. Do not invent missing details or claim the suite is complete or usable beyond the evidence.

## Construct honest, minimal cases

- Start from a contract-valid baseline that traverses the intended production flow. Set only verified prerequisites and
  behavior-relevant data. Never add an internal flag, timestamp, label, delay, or unrelated field merely to steer the
  current implementation toward green.
- For an error case, change one documented precondition from the valid baseline. Assert the intended error category and
  material postconditions, including absence of writes, sends, locks, leaked tasks, or state changes when relevant; do
  not let an earlier unrelated failure satisfy the case.
- Prefer meaningful, non-default, distinguishable values so an empty/default implementation cannot pass accidentally.
  Cover normal flow and justified equivalence classes such as below/at/above a boundary, empty/missing/malformed input,
  partial failure, retry, idempotence, cancellation, rollback, and cleanup. Do not create a mechanical cross-product.
- Obtain the actual result through the real public or generated entry point. Derive expected values independently with
  literals, a hand-checked fixture, or an independent oracle; never compute both sides with the code under test or a
  helper that copies its algorithm.
- Mirror the complete contract shape in fixture and mock data — every field the current schema/protocol documents, not
  only the fields the current assertion reads; a partial fixture passes while a downstream read breaks. A full-shadow
  fixture such as the `feature::resource` mock excel provider replaces the real bindir entirely (no fallback), so seed
  every table the startup and flow require; an unseeded table is an explicit failure, not a leaner test.
- Assert behavior, not text: run the generated code, template render, or script against controlled inputs and assert
  outputs, exit codes, or side effects. Do not grep artifact source for an expected line when the artifact can be built
  or executed.
- Keep one coherent behavior per case, but assert all material outcomes of that behavior. Reuse setup only when it hides
  irrelevant plumbing, not the scenario inputs or expected result. Prefer a small table only for genuinely equivalent
  input classes; do not hide branch-specific failures behind one loop.
- Keep test-only cleanup/access helpers in test support instead of adding production API. If mock setup dominates the
  scenario or cannot preserve required side effects, use the real in-process component or move to integration coverage.

## Keep dependencies deterministic

- Exercise the real system under test. Prefer real dependencies only when fast and deterministic; isolate network, DNS,
  DB, wall clock, randomness, external services, and thread scheduling with existing project fakes, virtual clocks,
  in-memory backends, event pumps, or explicit synchronization seams.
- Do not use a fixed sleep, CPU/network jitter, internal pump count, or arbitrary timeout as proof of correctness. For
  real I/O behavior, wait on an observable predicate; a wall-clock timeout is only a hang guard and must be followed by
  an explicit assertion of the predicate and business result.
- Advance logical time explicitly through an existing clock seam. Use a reproducible seed when an existing API supports
  one; otherwise assert stable invariants instead of exact random output.
- Mock at the slow/external boundary only after inspecting the real dependency's relevant side effects. Use real
  generated protobuf/schema types and a minimal contract-valid baseline. Assert calls, order, or counts only when they
  are protocol/business behavior; otherwise assert the SUT's output, state, payload, and cleanup. A mock or fake earns
  no assertions: its mere presence or invocation proves nothing about the SUT — assert the real component's observable
  result or unmock it.
- Treat real services, databases, and network environments as integration prerequisites. Pin and report them; an
  unavailable or skipped dependency is not passing unit coverage.

## Follow project conventions

- Use the nearest healthy tests for `CASE_TEST(group, case)` naming, fixture ownership, helper selection, CMake
  registration, and cleanup. New names should be lower_snake_case where the local group uses it and describe the
  scenario/result; do not mass-rename legacy cases.
- Use the smallest Arrange-Act-Assert path that still exposes intent. Keep decisive inputs and expected outcomes visible
  in the case. Do not copy a nearby fragile sleep, fixed port, or implementation-shaped mock merely for consistency.
- `CASE_EXPECT_*` is non-fatal in the shared framework variants. After a failed setup/precondition assertion, guard
  dependent work or return after required cleanup; do not continue into invalid state.
- Apply `engineering-guidelines` for C++/CMake style and validation instead of duplicating those rules here.

## Repair discipline

- When a case fails, fix the implementation or redesign the case from evidence. Never weaken or delete an existing
  assertion, loosen an expected value, skip or disable a case, add a retry, or widen a timeout merely to reach green.
- A defect case that passes on first run proves nothing about the fix; observe the intended RED or report that it was
  not observed.
- After repeated failed fixes, stop and re-evaluate per `change-workflow` instead of stacking patches or reshaping the
  case to fit the current implementation.

## Accept with fresh evidence

- For defects and behavior changes, follow `change-workflow`: when practical, run the focused case before implementation
  and confirm it fails for the intended reason, not setup, compilation, or another branch. If RED was not observed, say
  so rather than claiming test-first regression evidence.
- Run the exact case/group first and confirm it was selected, then the affected target/suite and broader coverage in
  proportion to risk. Repetition or shuffling may diagnose leaked global state/concurrency; a retry-pass remains flaky.
- Mentally mutate high-risk behavior: wrong branch/argument, missing validation or side effect, default return, and
  omitted cleanup. A focused assertion should catch each realistic mutation the selected scope claims to protect.
- Reject or redesign a case when: setup and assertion share the same object or builder; it fails only via crash or
  timeout and never on a wrong value; it fails on intentional refactors but not on realistic bugs; it greps source or
  generated text instead of executing the artifact; mock setup outweighs the scenario; or it exists only for a count.
- Read exit status and selected/passed/failed/skipped counts. Report source-derived conclusions, compilation, executed
  tests, skips, and unverified platforms/environments separately; never promote static review or one green run into
  evidence for work that did not execute.
