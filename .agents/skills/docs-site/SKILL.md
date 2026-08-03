---
name: docs-site
description: >
  Use when: building, running, or editing the Docusaurus documentation site under doc/, including architecture/development
  docs, i18n (zh-CN default + en), sidebar/navigation changes, or font/CSS customization. Do not use when: editing
  README.md/AGENTS.md or code comments only.
---

# Skill: docs-site

The documentation site lives in `doc/` and is a Docusaurus 3 project (docs-only mode, no blog). Sources of truth for
content are the C++ code under `src/`, `atframework/service/`, deployment templates under `install/`, and component
README/Note files. Regenerate docs from code, not from memory, when interfaces change.

## Layout

- `doc/docusaurus.config.js` — site config: i18n (`zh-CN` default, `en`), navbar/footer, font CDN `headTags`, mermaid.
- `doc/sidebars.js` — sidebar structure; every doc id must exist in both locales.
- `doc/src/css/custom.css` — Infima overrides and the font stacks (see "Fonts" below).
- `doc/docs/**` — default-locale documents (Simplified Chinese).
- `doc/i18n/en/**` — English translations: `docusaurus-plugin-content-docs/current/**` mirrors `docs/**` paths,
  `code.json` for UI strings, `docusaurus-theme-classic/*.json` for navbar/footer labels.
- `doc/static/` — static assets (logo, images). Mermaid diagrams are preferred over image files for architecture.

## Commands (Windows PowerShell)

PowerShell blocks `npm.ps1`/`pnpm.ps1` on this machine; always call `npm.cmd` / `pnpm.cmd`:

```powershell
cd doc
pnpm.cmd install       # first time only (npm.cmd install also works; both lockfiles are kept)
pnpm.cmd start         # dev server (default locale), hot reload
pnpm.cmd start --locale en   # dev server for the English locale
pnpm.cmd build         # production build for ALL locales -> doc/build/
pnpm.cmd serve         # preview the production build
```

Notes:

- pnpm does NOT strip a `--` separator like npm does; pass script args directly (`pnpm.cmd start --locale en`,
  not `-- --locale en`).
- `start`/`serve` pin `--host 127.0.0.1` in package.json: this machine's IPv6 loopback is broken (ping ::1 fails),
  so the default `--host localhost` binds ::1 only and the page is unreachable. Remove the flag if LAN access or
  IPv6 is needed on a healthy machine.
- pnpm 11 reads settings from `doc/pnpm-workspace.yaml` (the package.json `pnpm` field was removed in v11).
  Dependency build scripts are disallowed by default (`strictDepBuilds`); review new postinstall scripts and
  record decisions in the `allowBuilds` map there (core-js's funding banner is disallowed on purpose).

`pnpm.cmd build` (or `npm.cmd run build`) must pass before a docs change is done; it validates broken links and
i18n parity. Missing translations intentionally fall back to the default locale, but keep both locales in sync for
all shipped pages.

## i18n rules

- Write the Chinese (default) version first in `doc/docs/`, then translate into `doc/i18n/en/docusaurus-plugin-content-docs/current/`.
- Keep identical relative file paths, identical heading structure/anchors, identical code blocks, and identical mermaid
  diagrams (translate only node labels inside diagrams).
- Keep technical terms consistent: dispatcher/task action/router/RPC/WAL/atgateway/atproxy stay in English in both locales.
- When adding UI labels (navbar/footer), update `docusaurus.config.js` and both locales' theme JSON files.

## Fonts

Stacks live at the top of `doc/src/css/custom.css` (`--docs-font-sans`, `--docs-font-mono`):

- English faces are imported from CDN via `headTags` in `docusaurus.config.js` (jsDelivr Fontsource, China-reachable;
  do NOT use Google Fonts hosts). Only the highest-priority English face of each stack is imported.
- Chinese faces are local-install fallbacks in priority order (Sarasa/更纱黑体 -> Noto CJK -> Maple Mono -> ...). Font
  family names include both the Chinese and English family names (e.g. `"等距更纱黑体 SC", "Sarasa Mono SC"`).
- Body stack: CDN `Noto Sans` first, then the configured Chinese chain, then `sans-serif`. Code stack: CDN
  `Noto Sans Mono` first, then Maple Mono variants, then the Chinese chain, then `monospace`.

## Conventions

- Keep docs concise and source-backed; reference code with `path/to/file` (no deep line numbers that drift).
- One topic per page; use the existing sidebar categories (getting-started / architecture / components / services /
  development) instead of creating new top-level sections without user approval.
- Do not commit `doc/node_modules/`, `doc/build/`, or `doc/.docusaurus/` (already in `doc/.gitignore`).
