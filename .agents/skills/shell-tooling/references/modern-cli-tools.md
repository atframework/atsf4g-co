# Modern CLI tools

Prefer an installed tool that satisfies the task and the active harness.
Check its path, version, and relevant options before use. Use a native
cmdlet or traditional tool when the preferred tool is unavailable or has
different semantics. Keep automation non-interactive and machine-readable.

## Tool selection

| Tool | Use | Fallback or constraint |
| --- | --- | --- |
| `rg` | Search text and list files with `rg --files` | `Select-String` or `grep`. Check ignored, hidden, and binary files. |
| `ugrep` | Search with its grep-compatible options | Verify the flags the script uses. |
| `fd` | Filter paths by name or type | `Get-ChildItem` or `find`. Check hidden files and literal matching. |
| `bat` | Read files with line numbers | `Get-Content`. Disable color and paging for machines. |
| `sd` | Replace literal text or regular expressions | Verify matching, replacement groups, and file encoding. Use the harness patch interface when required. |
| `eza` | List files and shallow trees | `Get-ChildItem` or `ls`. Avoid parsing icons or colors. |
| `erd` | Browse trees with disk usage | `tree` and `du`. Keep scans scoped. |
| `dust` | Locate large directories | `du`. Distinguish allocated space from file size. |
| `duf` | Inspect filesystem capacity | `df`. Check mount and container visibility. |
| `hyperfine` | Benchmark repeated commands | `Measure-Command` or `time`. Control warmup and shell. |
| `tokei` | Count code and comment lines | `cloc`. Record exclusions. |
| `hexyl` | View a bounded binary excerpt | `xxd`. Limit output that may contain sensitive data. |
| `jq` | Query or transform JSON | Project parser or PowerShell JSON cmdlets. Preserve types and exit codes. |
| `jaq` | Run a verified compatible jq filter | Use jq when a filter or module behaves differently. |
| Mike Farah `yq` | Query or update YAML | Verify the implementation and preserve needed comments and types. |
| `mlr` | Process CSV and TSV records | Use a real CSV parser for quoted delimiters. |
| `qsv` | Validate or transform CSV | Check command options and memory use on large inputs. |
| `delta` | Review Git diffs | Use raw `git diff` for machine processing. |
| `difft` | Inspect syntax-aware differences | Use `git diff` for scope and patch verification. |
| `tspin` | Highlight log output | Use the original log for parsing. Check follow mode and paging. |
| `lnav` | Inspect or query structured logs | Use `lnav -n` for headless mode where installed. |
| `plocate` | Find names in an existing Linux index | Use `fd` or `rg --files` when the index is stale or absent. |
| `pigz` | Create gzip data in parallel | Check the thread budget and output format. |
| `zstd` | Compress data with Zstandard | Verify that the consumer supports it before changing formats. |
| `ouch` | Inspect or extract supported archives | Verify the format and destination before extraction. |
| `aria2c` | Download or resume files | `curl` or `wget`. Honor rate limits and verify downloaded artifacts. |
| `fzf` | Filter a candidate list | Use `--filter` in automation and `rg` for exact matching. |
| `xh` | Inspect HTTP requests and responses | Use `curl` or HTTPie when request or exit semantics differ. |
| `doggo` | Inspect DNS records | `dig` or `Resolve-DnsName`. Specify resolver and record type. |
| `procs` | Find and inspect processes | `Get-Process` or `ps`. Confirm visible fields and permissions. |
| `watchexec` | Watch files during development | Scope watched paths and stop owned children when finished. |

On Windows, use `hyperfine --shell pwsh` when the benchmark requires
PowerShell syntax. Linux package names may differ from executable names,
such as `fd-find`/`fdfind` and `bat`/`batcat` on Debian. On macOS,
`mdfind` can query an existing Spotlight index.

## Installation

For an authorized install, prefer a trusted platform package or official
release that matches the version, OS, and architecture. Check package and
binary names through the package manager. Verify available signatures or
checksums before running a downloaded executable. A version manager can
select a tool but does not guarantee a prebuilt artifact for each platform.

`cargo binstall` can fall back to compilation. When only binaries are
allowed, verify and select its no-compilation policy. Use a suitable
installed fallback for optional tools.
[cargo-binstall documentation](https://github.com/cargo-bins/cargo-binstall)

## Local probe

A Windows probe on 2026-11-28 found `rg` 15.0.0, `fd` 10.4.2,
`sd` 1.0.0, `jq` 1.8.2, Mike Farah `yq` 4.54.3, and `bat` 0.26.1.
These are observed versions. Probe again after an environment change.
