# Accessibility Testing

Wixen's whole purpose is to be usable by people who cannot see the screen, so
accessibility is tested at two levels. Automated checks catch the structural
half of WCAG. Manual testing with real assistive technology catches the rest,
which is most of the lived experience. Neither replaces the other.

The guiding rule: structure present is not experience good. A node can exist in
the accessibility tree, pass every automated check, and still be useless or
confusing to a real screen reader user. Automation tells you the plumbing is
connected. Only a person using the tool tells you it works.

## Automated checks (the structural half)

These run in CI from `.github/workflows/a11y-desktop.yml`.

### UIA structure and Axe.Windows scan

`tests/a11y-uia/` is a .NET test project that launches the built `wixen.exe` and
inspects its UI Automation tree, the same API a screen reader uses. It asserts:

- the main window exposes a non-empty Name,
- the terminal content is reachable and exposes a text or grid pattern,
- every focusable control has a Name,
- Axe.Windows (the desktop analog of the axe web scanner) reports no errors.

These catch the bug this project actually hit: a provider node that exists in
code but is not reachable or not named through UIA.

Run it locally on Windows:

```powershell
cargo build --release --bin wixen
dotnet test tests/a11y-uia/WixenA11y.Tests.csproj -c Release
```

The scan needs a headed session (a real desktop). It will not work on a bare
headless runner because UIA has no window to inspect.

### NVDA speech smoke test (experimental)

`tests/nvda-smoke/` drives a real NVDA through Guidepup and checks that NVDA
speaks something when the terminal starts and receives output. It is flaky by
nature, needs a headed session, and never blocks a merge. It is a starting
point. The real assertions about what NVDA should say (command blocks, exit
codes, table cells) are yours to add against real runs.

```powershell
cd tests/nvda-smoke
npm install
$env:WIXEN_EXE = "..\..\target\release\wixen.exe"
npm run smoke
```

## Manual testing (the half automation cannot reach)

Do this before any accessibility feature is considered done. Automated tools
cover roughly half of WCAG success criteria; the other half, and all of the
judgment about whether the experience is good, needs a person.

- **Screen reader walkthrough (NVDA and Narrator).** Launch the terminal, run a
  few commands, and confirm you can understand the prompt, the command, the
  output, and the exit status by ear alone. Navigate command blocks, tables, and
  links. Confirm nothing is announced twice, out of order, or not at all.
- **Keyboard only.** Unplug the mouse. Reach every control and action. Confirm a
  visible focus indicator, a sensible tab order, and that shortcuts follow
  Windows conventions.
- **Low vision.** Check color contrast against the theme, confirm no information
  is carried by color alone, and test at 200% scaling.
- **Reduced motion and audio.** Confirm `prefers-reduced-motion` is honored, that
  every audio cue has a visual equivalent, and that the cues are distinct (on
  versus off, error versus warning) and do not flood under heavy output.

## Tools reference

- **Axe.Windows / AxeWindowsCLI** and **Accessibility Insights for Windows**:
  scan the UIA tree for WCAG-mapped issues.
- **FlaUI**: UIA automation for asserting Name, Role, Value, and patterns.
- **Guidepup**: automates real NVDA and VoiceOver and captures spoken output.
- **Inspect.exe** and **AccEvent.exe** (Windows SDK): manual inspection of the
  UIA tree and events.

## Status

The automated project and workflow are scaffolding. Validate them on a real
Windows machine first: confirm the package versions, the Axe.Windows API surface,
and that the app presents a window under the CI session. See the notes in
`tests/a11y-uia/WixenA11y.Tests.csproj` and the workflow file.
