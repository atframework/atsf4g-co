# Documentation theme and fonts

Read this only when changing `doc/src/css/custom.css`, font loading, Docusaurus `headTags`, or typography.

Font stacks are defined by `--docs-font-sans` and `--docs-font-mono` near the top of `custom.css`.

- English faces are loaded through `headTags` in `docusaurus.config.js` from the existing jsDelivr Fontsource route;
  do not introduce Google Fonts hosts. Import only the highest-priority English face for each stack.
- Chinese faces are local-install fallbacks. Preserve both Chinese and English family aliases where the current stack
  uses them.
- Keep the body stack ordered as the configured English face, Chinese fallback chain, then `sans-serif`; keep the code
  stack ordered as the configured mono face, Maple Mono variants, Chinese fallback chain, then `monospace`.

Verify both locales and representative prose/code blocks after a typography change. Check network failures and fallback
rendering rather than assuming the CDN face is always available.
