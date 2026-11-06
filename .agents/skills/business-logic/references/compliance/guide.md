# Compliance Guide

Use for compliance-svr, `rpc::compliance`, text-filter dictionary, and caller filtering-policy work. Read current
source before changing behavior; the public integration guide is `doc/docs/components/compliance.md`.

## Authoritative sources

- `src/component/compliance/protocol/protocol/pbdesc/compliance_service.proto`: batch protocol and result ordering.
- `src/component/compliance/compliance_svr/logic/action/task_action_filter_text.cpp`: per-item matching, overlap
  handling, replacement, and whole-batch failure behavior.
- `src/component/compliance/compliance_svr/logic/text_filter_manager.h/.cpp`: dictionary load, reload, and snapshots.
- `src/component/compliance/sdk/rpc/compliance/compliance_client_api.h/.cpp`: discovery, routing, empty batches, and
  SDK failure behavior.
- `src/component/compliance/test/`: service/SDK tests and dictionary fixtures.
- `src/component/compliance/README.md`: dictionary generation and skip-policy details.

## Review checks

- Keep `results` aligned with request items by index. Any item failure clears the service response; do not return
  partial successes. Overlapping matches merge before replacement, while adjacent matches remain separate.
- Empty SDK batches clear the response and succeed without service discovery. Non-empty batches use the first text as
  the consistent-hash key. Other SDK failures do not clear the response, so callers must check the RPC result.
- Distinguish a successful `OK` result, a successful `MATCHED` result, and an unavailable result. The SDK does not
  decide whether unfiltered text may pass; verify the caller's fallback and empty-replacement policy.
- Preserve one dictionary snapshot per batch. Startup load failure, reload failure, and an unavailable dictionary are
  different states; failed reloads retain the old dictionary.
- Keep `compliance-svr` deployment resources and `compliance_svr.text_filter_dictionary_file` consistent with the
  process working directory. Regenerate and verify a new dictionary file before replacing the published resource.
- Run `ctest --test-dir <BUILD_DIR> -L compliance --output-on-failure` for service or SDK changes. Tests use fixtures
  under `src/component/compliance/test/data/` and do not require the dictionary generator.
- Update `src/component/compliance/README.md` for dictionary procedure changes and both compliance documentation
  locales for public behavior, protocol, configuration, or deployment changes.
