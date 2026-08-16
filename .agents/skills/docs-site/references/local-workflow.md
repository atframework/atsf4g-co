# Documentation site local workflow

Read this only when installing dependencies, running the development server, serving a build, or validating the full
Docusaurus site.

On this Windows workspace, PowerShell may block `npm.ps1`/`pnpm.ps1`; call the `.cmd` launchers:

```powershell
Set-Location doc
pnpm.cmd install
pnpm.cmd start
pnpm.cmd start --locale en
pnpm.cmd build
pnpm.cmd serve
```

- Pass pnpm script arguments directly (`pnpm.cmd start --locale en`), without npm's extra `--` separator.
- `start` and `serve` currently pin `--host 127.0.0.1` in `package.json` because IPv6 loopback is unreliable on this
  machine. Re-read the script before changing host behavior.
- pnpm settings live in `doc/pnpm-workspace.yaml`. Review new dependency build scripts and update `allowBuilds` only
  after verifying the postinstall behavior.
- `pnpm.cmd build` builds all locales and checks broken links. Read the exit code and diagnostics; do not infer i18n
  parity from a default-locale development server.
