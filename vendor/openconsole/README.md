# Bundled OpenConsole binaries

This directory receives two unmodified Microsoft binaries, fetched by
`scripts/fetch-openconsole.ps1`:

- `OpenConsole.exe`
- `OpenConsoleProxy.dll`

They are taken as-is from the Windows Terminal **v1.24.11911.0** release
asset `Microsoft.WindowsTerminal_1.24.11911.0_x64.zip` (and the matching
`_arm64.zip` for ARM64 builds) published at
<https://github.com/microsoft/terminal/releases>. We download the prebuilt
binaries; we never compile Windows Terminal. The fetch script verifies each
file against a pinned SHA256 hash before it lands here, and the binaries
themselves are not committed to this repository.

## Why Wixen bundles them

Becoming the default terminal on Windows 11 requires more than the terminal
window itself. When a console application starts, Windows delegates the
session to a *console server* (named by the `DelegationConsole` registry
value) which owns the ConPTY plumbing, and that server hands the connection
to a *terminal* (named by `DelegationTerminal`) for display. The inbox
`conhost.exe` predates this handoff protocol on some servicing levels, so
Microsoft ships an out-of-band console server — OpenConsole — with Windows
Terminal. Wixen bundles the same server so the handoff hop works regardless
of the inbox conhost version.

- `OpenConsole.exe` — the console server that owns the ConPTY session and
  performs the handoff. CLSID: `{2EACA947-7F5F-4CFA-BA87-8F7FBEEFBE69}`.
- `OpenConsoleProxy.dll` — the COM proxy/stub for the `ITerminalHandoff`
  interface family, needed to marshal the handoff calls between processes.
  CLSID: `{3171DE52-6EFA-4AEF-8A9F-D02BD67E7A4F}`.

Wixen's default-terminal registration points these CLSIDs at the copies
installed alongside `wixen.exe`, so the handoff never depends on Windows
Terminal being installed. See `docs/default-terminal.md` for the end-to-end
flow and the safety model.

## License

Windows Terminal, including OpenConsole, is published by Microsoft under the
MIT license. The full license text follows.

```
MIT License

Copyright (c) Microsoft Corporation. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

This file is installed next to the binaries (as `THIRD-PARTY-NOTICES.md`) to
satisfy the MIT license's requirement that the copyright and permission
notice accompany copies of the software.

## Updating the pinned version

1. Edit `$WtVersion` (and this file) to the new release.
2. On a trusted machine, run `scripts/fetch-openconsole.ps1 -UpdateHashes`
   for each architecture and confirm the printed download URL is the official
   `microsoft/terminal` release.
3. Pin the printed SHA256 values in the script's `$ExpectedHashes` map.
4. Re-run the script without flags; it must report all hashes verified.
