# Desktop accessibility tests (UIA + Axe.Windows)

A .NET test project that launches the built `wixen.exe` and asserts, through UI
Automation, the accessibility structure a screen reader depends on. Full context
and the manual testing these do not replace are in
[docs/accessibility-testing.md](../../docs/accessibility-testing.md).

## Run

```powershell
cargo build --release --bin wixen
dotnet test WixenA11y.Tests.csproj -c Release
```

Needs a headed Windows session; UIA has no window to inspect on a bare headless
runner.

## Scaffolding note

This has not been executed yet. Before relying on it, confirm on a real Windows
machine: the NuGet package versions in the `.csproj`, the Axe.Windows.Automation
API surface used in `TerminalAccessibilityTests.cs` (the `AxeWindows_FindsNoErrors`
test), and that the app presents a window under your CI session.
