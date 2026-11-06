---
name: business-logic
description: "Use when: developing, diagnosing, or reviewing matchmaking/team, chat/mail/compliance, item-change source, or Orbit room business behavior. Do not use for generic RPC/DTMQ, build, deployment, or tooling work."
---

# Business Logic

Read only the module guide needed for the task. A service name or shared dependency alone does not require loading
business guidance. Paths in code spans in the module guides are repository-relative unless explicitly stated otherwise.

| Business scope | Read when needed |
| --- | --- |
| Matchmaking: matchsvr/lobbysvr pools, rules, factions, level selection, WAL migration, Orbit handoff, Matching.xlsx, and tests | [Matching guide](references/matching/guide.md) |
| Teams: Lobby integration, Team Room membership/admission, CS dirty notifications, and repair tests | [Team guide](references/team/guide.md) |
| Compliance: batch text filtering, dictionary lifecycle, SDK routing, and caller fallback policy | [Compliance guide](references/compliance/guide.md) |
| Chat: world/private/team channels, CS sync, DTMQ persistence, team notifications, and filtering policy | [Chat guide](references/chat/guide.md) |
| Mail: user/global delivery, templates, CS sync, attachment receipt, and mail-item sources | [Mail guide](references/mail/guide.md) |
| Items: container operations, dirty sync, operation sources, and OSS flow boundaries | [Item guide](references/item/guide.md) |
| Orbit: room/user lifecycle, Match/Lobby handoff, DS callbacks, and room events | [Orbit guide](references/orbit/guide.md) |

For matchmaking reviews or fixes, also read the relevant sections of the dated
[review baseline](references/matching/review-baseline.md); verify each finding against current source before using it.
Load both module guides only when the task crosses team and matchmaking behavior, such as team-ready/start-matching
callbacks. For an unlisted business module, locate its owning source with the [source index](../../source-index.md)
only if the repository map is insufficient. Do not load unrelated module guides.
