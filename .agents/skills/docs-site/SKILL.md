---
name: docs-site
description: "Use when: editing or building the Docusaurus site under doc/, including content, zh-CN/en i18n, navigation, or theme CSS. Do not use for root README/AGENTS/Skill Markdown or code comments."
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

## Local workflow

Read [local development and build](references/local-workflow.md) only when installing dependencies, starting or serving
the site, or running the production build. A shipped docs change requires a fresh all-locale build unless the environment
blocks it; report that boundary explicitly.

## i18n rules

- Write the Chinese (default) version first in `doc/docs/`, then translate into `doc/i18n/en/docusaurus-plugin-content-docs/current/`.
- Keep identical relative file paths, identical heading structure/anchors, identical code blocks, and identical mermaid
  diagrams (translate only node labels inside diagrams).
- Keep technical terms consistent: dispatcher/task action/router/RPC/WAL/atgateway/atproxy stay in English in both locales.
- When adding UI labels (navbar/footer), update `docusaurus.config.js` and both locales' theme JSON files.

## Theme and fonts

Read [theme and fonts](references/theme-and-fonts.md) only when changing `custom.css`, Docusaurus `headTags`, font
loading, or typography. Do not load it for ordinary content or navigation edits.

## Conventions

- Follow the root writing rule. Keep docs concise and source-backed; reference code with `path/to/file` instead of deep
  line numbers that drift.
- One topic per page; use the existing sidebar categories (getting-started / architecture / components / services /
  development) instead of creating new top-level sections without user approval.
- Do not commit `doc/node_modules/`, `doc/build/`, or `doc/.docusaurus/` (already in `doc/.gitignore`).
