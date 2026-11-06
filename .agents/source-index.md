# Repository Source Index

Use this file only when the repository map and a task-matching Skill do not reveal where authoritative evidence lives,
or when a task crosses several subsystems. It is a current-state routing map, not an exhaustive file inventory, a
history log, or startup context.

## Reading order

1. Read the nearest `AGENTS.md` and select a Skill from exposed metadata; use `skills/README.md` only if discovery is
   unavailable.
2. Follow the matching `SKILL.md` and its explicit reference-loading conditions.
3. Use the table below only to locate evidence that the Skill does not already identify, then open the smallest relevant
   set of files.
4. Treat current code, schemas, templates, configs, tests, and build state as authoritative. Documentation explains the
   intended design but does not override observable implementation or active configuration.

For published documentation discovery, read `../doc/sidebars.js` as the catalog. Open only the relevant page under
`../doc/docs/`; read the English mirror under `../doc/i18n/en/` only when editing translations or checking locale parity.

## Task-to-source map

| Need | Read first | Orientation only | Recheck when |
| --- | --- | --- | --- |
| Repository architecture or service flow | Relevant `../src/**`, protocol schemas, CMake ownership, and focused tests | `../doc/docs/architecture/`, `../doc/docs/components/`, `../doc/docs/services/` | Runtime path, schema, or ownership changed |
| C++/CMake/protobuf conventions or review | Nearest `AGENTS.md`; root or subproject `.clang-format`, `.clang-tidy`, `.editorconfig`, and `CPPLINT.cfg`; affected code/tests | `skills/engineering-guidelines/SKILL.md` and its task-specific section | Scope enters a subproject or a config changed |
| Configure or build state | `../.vscode/settings.json`, relevant `CMakeLists.txt`, and the selected build tree's `CMakeCache.txt` | `skills/build/SKILL.md`, `../doc/docs/getting-started/build.md` | Every new tree, toolchain, generator, or option set |
| Protobuf, RPC, or generated code | Owning `.proto`, `../src/templates/`, `../src/tools/generate-for-pb/`, and generation CMake rules | `skills/engineering-guidelines/code-generation.md`, `../doc/docs/architecture/rpc-codegen.md` | Schema, template, generator, or output ownership changed |
| Generic tests | Owning test source and `CMakeLists.txt`; executable help/list output | `skills/testing/SKILL.md`, `../doc/docs/development/testing.md` | Test target or runner behavior changed |
| Offline service RPC tests | `../src/tools/rpc-unit-test/` headers, tests, `CMakeLists.txt`, and `README.md` | `skills/rpc-unit-test/SKILL.md`, `../doc/docs/development/rpc-unit-test.md` | Fixture APIs, engines, hooks, or feature flags changed |
| Authentication and account storage | `../src/authsvr/service/logic/algorithm/`, `../src/authsvr/service/app/authsvr_main.cpp`, auth config, and `svr.local.table.proto` | `../doc/docs/services/auth.md`, `../doc/docs/architecture/data-layer.md` | Login policy, credential handling, account GM, or login-key layout changed |
| Lobby team integration and notifications | `../src/lobbysvr/service/logic/team/`, `../src/teamsvr/service/room/logic/room/team_room.cpp`, and team protobuf schemas | `skills/business-logic/references/team/guide.md`, `../src/lobbysvr/service/logic/team/README.md` | Team protocol, dirty flow, membership, or admission repair changed |
| Compliance text filtering | `../src/component/compliance/README.md`, service/SDK sources, and `../src/component/compliance/protocol/protocol/pbdesc/compliance_service.proto` | `skills/business-logic/references/compliance/guide.md`, `../doc/docs/components/compliance.md` | Batch protocol, dictionary lifecycle, caller fallback, or deployment changed |
| Chat and team chat | `../src/lobbysvr/service/logic/chat/`, `../src/teamsvr/sdk/common/rpc/team/`, and chat/DTMQ protobuf and Excel sources | `skills/business-logic/references/chat/guide.md`, `../doc/docs/components/chat.md` | Channel key, sync, persistence, notification, or filtering policy changed |
| Lobby mail | `../src/lobbysvr/sdk/rpc/mail/`, `../src/lobbysvr/service/logic/mail/`, and `../resource/ExcelTables/Mail.xlsx` | `skills/business-logic/references/mail/guide.md`, `../doc/docs/components/mail.md` | Delivery, template, CS sync, refresh, or attachment behavior changed |
| Item operations and sources | `../src/component/GameSharedComponent/ItemAlgorithm/`, `../src/lobbysvr/service/logic/item/`, and item/OSS protobuf schemas | `skills/business-logic/references/item/guide.md`, `../doc/docs/components/item.md` | Container transactionality, source propagation, dirty sync, or flow emission changed |
| Orbit room and user lifecycle | `../src/orbitsvr/service/logic/room/`, `../src/lobbysvr/service/logic/orbit/`, and Match Orbit callbacks | `skills/business-logic/references/orbit/guide.md`, `../doc/docs/components/orbit.md` | Room state, handoff, settlement, event channel, or AutoClient integration changed |
| atgateway v2 wire behavior | `../atframework/service/atgateway/protocol/PROTOCOL.md`, live schema, SDK implementation, and protocol tests | `skills/atgateway-protocol/SKILL.md`, `../doc/docs/architecture/gateway-proxy.md` | Wire schema, state machine, crypto, or compatibility changed |
| Expression-enabled configuration | libatapp's nearest `AGENTS.md`, `include/atframe/atapp_conf.proto`, `src/atframe/atapp_conf.cpp`, and matching tests | `skills/configure-expression/SKILL.md`, `../doc/docs/architecture/configuration.md` | Annotation or expansion semantics changed |
| Deployment rendering | `../install/cloud-native/charts/`, `../install/cloud-native/values/`, renderer inputs, and representative rendered output | `skills/deployment-config/SKILL.md`, `../doc/docs/getting-started/run-deploy.md` | Template, values layering, renderer, or target syntax changed |
| Documentation site | Source code/config being documented, `../doc/sidebars.js`, `../doc/docusaurus.config.js`, and both locale files | `skills/docs-site/SKILL.md` | Navigation, source behavior, locale structure, or build config changed |
| MCP integration (tgrep/CodeGraph wrappers, installer) | `../project/integration/mcp/README.md`, affected wrapper sources/tests | `skills/mcp-integration-maintenance/SKILL.md` | Upstream pins, agent config formats, lifecycle, or installer behavior changed |
| AI guidance and Skills | `../AGENTS.md`, Skill metadata, affected `SKILL.md`, references, and thin bridges | `skills/ai-agent-maintenance/SKILL.md` | Discovery behavior, trigger scope, or source ownership changed |

When a path moves, update the owning Skill first if it is task-specific; update this map only when the cross-task route
also changed. Do not copy long procedures or external research into this file.
