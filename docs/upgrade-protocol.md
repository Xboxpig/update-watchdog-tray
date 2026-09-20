# Package Upgrade Protocol

`scripts/update-cc-switch` and `scripts/update-chatgpt-desktop` write the same
tab-separated event stream to stdout. The tray application treats it as a
stable local API and never interprets command output as progress.

| Event | Fields after event name | Meaning |
| --- | --- | --- |
| `PROGRESS` | percent, stage, detail, cancelable | Stage transition. `cancelable` is `true` or `false`. |
| `LOG` | level, detail | Diagnostic line for the expandable UI log. |
| `WARNING` | code, title, detail | Non-fatal condition. |
| `ERROR` | code, title, detail | Terminal failure. |
| `RESULT` | status, version, detail | Terminal successful result. Status is `success` or `already-current`. |

## Error codes

| Code | Meaning | User action |
| --- | --- | --- |
| `E_PRECONDITION` | A required command, directory, log, or temporary workspace is unavailable. | Correct the named local dependency or permission. |
| `E_CONCURRENT` | Another upgrade holds the local lock. | Wait for it to finish. |
| `E_INSTALLED_VERSION` | The installed package cannot be read or parsed. | Repair or reinstall the named package. |
| `E_RELEASE_METADATA` | Authoritative release metadata could not be retrieved. | Check the network and try again. |
| `E_RELEASE_VERSION` | The release version is missing, malformed, or incomparable. | Retry later; report an upstream metadata issue if persistent. |
| `E_RELEASE_ASSET` | The x86_64 Debian asset, size, or SHA-256 digest is unavailable or inconsistent. | Wait for a complete upstream release. |
| `E_DOWNLOAD` | The selected asset could not be downloaded. | Check network connectivity and retry. |
| `E_CHECKSUM` | Downloaded bytes do not match GitHub's SHA-256 digest. | Retry; do not install the file manually. |
| `E_ARCHIVE` | The downloaded Debian archive cannot be read. | Retry; report a corrupt upstream release if persistent. |
| `E_TEMPLATE` | The installed local pacman packaging template is missing or invalid. | Reinstall Update Watchdog. |
| `E_BUILD` | `makepkg` could not create the local package. | Open the diagnostic details and fix the reported build issue. |
| `E_PATCH` | The generated ChatGPT package is missing the appmenu `DT_NEEDED` workaround. | Do not install the package; reinstall or repair Update Watchdog. |
| `E_AUTH` | Administrator authentication was unavailable, rejected, or cancelled. | Authenticate and retry. |
| `E_INSTALL` | pacman rejected the generated package. | Inspect diagnostic details; the prior installed package remains active. |
| `E_POST_INSTALL` | pacman completed but version or file-integrity verification failed. | Do not restart CC Switch; repair the package first. |
| `E_CANCELLED` | The user cancelled before the non-cancellable installation stage. | Retry when ready. |
| `E_PROTOCOL` | The helper exited without a valid terminal event. | Reinstall Update Watchdog and inspect its log. |

## Warnings

`W_SYSTEM_HOOK` reports a non-fatal pacman hook failure such as Timeshift failing
to create a Btrfs snapshot. It does not change the package-install result.
