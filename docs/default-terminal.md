# Default terminal on Windows 11

How Wixen becomes the terminal Windows launches for console applications, what
gets bundled and registered to make that work, and why the feature stays gated
until a live test passes.

## How Windows picks a terminal

When a console application starts outside an existing terminal (for example,
from the Run dialog or Explorer), Windows 11 consults two `REG_SZ` values under
`HKCU\Console\%%Startup`:

- `DelegationConsole` — the CLSID of the *console server* that owns the ConPTY
  session for the new process.
- `DelegationTerminal` — the CLSID of the *terminal* that displays it.

Windows `CoCreateInstance`s the console server, which creates the ConPTY
plumbing and then calls the terminal's `ITerminalHandoff` COM interface to hand
the connection over. Both CLSIDs must resolve to working COM registrations or
the handoff cannot complete.

## What gets bundled

The console-server half of the hop is not Wixen code. Microsoft ships an
out-of-band console server, OpenConsole, with Windows Terminal, because the
inbox `conhost.exe` cannot be assumed to support the handoff protocol on every
servicing level. Wixen bundles the same two prebuilt, unmodified,
MIT-licensed binaries from the pinned Windows Terminal release (v1.24.11911.0):

| File | Role | CLSID |
|---|---|---|
| `OpenConsole.exe` | Console server: owns ConPTY, initiates the handoff | `{2EACA947-7F5F-4CFA-BA87-8F7FBEEFBE69}` |
| `OpenConsoleProxy.dll` | COM proxy/stub that marshals `ITerminalHandoff` calls between processes | `{3171DE52-6EFA-4AEF-8A9F-D02BD67E7A4F}` |

`scripts/fetch-openconsole.ps1` downloads them from the official
`microsoft/terminal` GitHub release, verifies pinned SHA256 hashes, and places
them in `vendor/openconsole/`. Both installers (`installer/wixen.wxs`,
`installer/wixen.iss`) and the portable zip install them alongside `wixen.exe`,
together with `THIRD-PARTY-NOTICES.md` carrying the MIT license text. We never
compile Windows Terminal; see `vendor/openconsole/README.md` for provenance and
the hash-pinning procedure.

## What the registration writes

Everything is per-user (`HKCU`); no administrator rights are involved. When
default-terminal support is enabled, registration consists of:

1. `HKCU\Console\%%Startup`:
   - `DelegationTerminal` = Wixen's terminal CLSID
     `{7b3ed7a4-e02b-4742-9e23-1d9d2a50c5a1}` (see
     `crates/wixen-ui/src/default_terminal.rs`).
   - `DelegationConsole` = the OpenConsole console-server CLSID above.
2. `HKCU\Software\Classes\CLSID\{7b3ed7a4-...}\LocalServer32` = the quoted
   path to the installed `wixen.exe` plus the `--handoff` flag, so COM can
   launch Wixen when no instance is running (see
   `crates/wixen-ui/src/handoff.rs`).
3. COM registrations that point the OpenConsole CLSID at the bundled
   `OpenConsole.exe` and the proxy CLSID at the bundled
   `OpenConsoleProxy.dll` in Wixen's install directory. Because the
   registration writer resolves these paths from its own install_dir — which
   contains all three files — the handoff works whether or not Windows
   Terminal is installed.

The result at launch time: Windows starts the bundled OpenConsole, OpenConsole
creates the ConPTY session, and hands it to Wixen through `ITerminalHandoff`
(marshalled by the bundled proxy). Wixen detects the COM launch via the
`--handoff` / `-Embedding` switches and attaches the session to a new window.

## Safety: graceful degradation

- **Un-registering is always safe.** `restore_default_terminal` writes the
  null CLSID (`{00000000-...}`, "let Windows decide") to both delegation
  values. Windows then falls back to the inbox console host, so console
  launches never break.
- **Failed handoff falls back to conhost.** If the delegation target cannot be
  created — the binaries are missing, the registration is stale, or the server
  fails to start — the console session is expected to open in a normal conhost
  window rather than being lost. This fallback is part of the acceptance
  criteria for the live test below, not an assumption we ship on.
- **The uninstallers remove the files they installed**; combined with the
  restore path, a removed Wixen leaves the system on the inbox console host.

## Gating: the one-time live test

Enabling `set_as_default_terminal` for users is gated on a one-time live test
on real Windows 11 hardware (the handoff interface definitions carry
`// VERIFY:` markers in `crates/wixen-ui/src/handoff.rs` for the same reason —
an incorrect vtable or IID does not crash, it silently breaks console
launches). The test must confirm:

1. With Wixen registered, launching a console app from Run/Explorer opens it
   in Wixen via the bundled OpenConsole.
2. Killing `wixen.exe` and launching again makes COM start Wixen through
   `LocalServer32` with `--handoff`.
3. Deleting the bundled binaries (simulated corruption) still produces a
   usable conhost window — graceful degradation holds.
4. `restore_default_terminal` returns the system to the out-of-box behavior.

Until all four pass, registration stays hidden from the UI and the bundled
binaries are inert payload.
