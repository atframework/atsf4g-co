---
title: Compliance Service
---

# Compliance Service

`compliance` filters UTF-8 text in batches. Its service type is `kComplianceSvr = 33`, and its deployment chart is
`compliance-svr`. The service implementation, SDK, and protocols live under
`src/component/compliance/compliance_svr/`, `src/component/compliance/sdk/`, and
`src/component/compliance/protocol/` respectively.

## Call Flow and Deployment

Consumers link `compliance-sdk`, include `rpc/compliance/compliance_client_api.h`, and call
`rpc::compliance::filter_text(ctx, request, response)`. The SDK selects a ready node from service discovery,
uses the first text item as the consistent-hash routing key, and sends the whole batch in one request.

Enable the `compliance-svr` instance and include `resource/compliance/text_filter.acv2` in the publish directory.
`compliance_svr.text_filter_dictionary_file` configures the dictionary path; its default is relative to the process
working directory.

## Batch Protocol

Each item in `ComplianceService/filter_text`'s `SSFilterTextReq.items` contains input text and settings:

- `return_original_text`: whether to return the original text in the result.
- `replacement_text`: replacement for each matched range; an empty string deletes the range.

`SSFilterTextRsp.results` corresponds to request items by index. `status` is `OK` or `MATCHED`, and
`filtered_text` contains the filtered output, which equals the input when nothing matches. Overlapping matches are
merged before replacement; adjacent matches are replaced separately. If any item fails, the service returns an error
and clears the results instead of returning partial successes.

The SDK clears `response` and returns success immediately for an empty batch. Other SDK failures do not clear the
response, so callers must check the RPC result instead of treating a stale response as the current result. The caller
decides whether unfiltered text may pass.

## Dictionary and Reload

`text_filter_manager` loads, reloads, and clears the dictionary. A startup load failure is reported; a failed reload
keeps the old dictionary; each batch uses one snapshot. Matching ignores ASCII case and can skip conservatively
allow-listed punctuation, whitespace, and formatting characters. Combining marks, join controls, and normal
components of other writing systems must not be skipped indiscriminately.

To update the dictionary, generate a file that does not already exist, verify it, and then replace the resource:

```text
ac_automation_tool build --input <UTF-8词表> --output <新词库> --nocase --skip-space --skip-invisible --skip-punctuation --skip-symbols
ac_automation_tool verify --input <新词库> --semantic
```

See `src/component/compliance/README.md` for the complete skip rules and dictionary maintenance procedure.

## Integration

Add this declaration to the consuming service:

```cmake
USE_COMPONENTS "compliance-sdk"
```

Callers must handle an unmatched result, a matched result with replacement text, and an unavailable RPC or result.
Whether replacement policy allows the original text to proceed is a business decision that the SDK does not make
implicitly.

## Validation

Enable `PROJECT_ENABLE_UNITTEST` and `PROJECT_SERVER_FRAME_ENABLE_UNIT_TEST_HOOKS`, build the compliance service
and SDK tests, then run:

```bash
ctest --test-dir <BUILD_DIR> -L compliance --output-on-failure
```

Tests use the small dictionaries under `src/component/compliance/test/data/` and do not require the dictionary tool.
