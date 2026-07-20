using System;
using System.IO;
using System.Linq;
using FlaUI.Core;
using FlaUI.Core.AutomationElements;
using FlaUI.Core.Definitions;
using FlaUI.UIA3;
using Xunit;

namespace WixenA11y.Tests;

/// <summary>
/// Launches the built wixen.exe and asserts the accessibility structure a screen
/// reader depends on, through UI Automation. Requires a headed Windows session.
///
/// These catch the class of bug this project actually hit: a provider node that
/// exists in code but is not reachable or not named through UIA. They do NOT
/// replace testing with a real screen reader (NVDA/Narrator) - automated checks
/// cover roughly half of WCAG. Structure present is not experience good.
/// </summary>
public sealed class TerminalAccessibilityTests : IDisposable
{
    private readonly Application _app;
    private readonly UIA3Automation _automation;

    public TerminalAccessibilityTests()
    {
        _app = Application.Launch(WixenExecutable.Locate());
        _automation = new UIA3Automation();
    }

    private Window MainWindow()
    {
        var window = _app.GetMainWindow(_automation, TimeSpan.FromSeconds(15));
        Assert.True(window is not null, "wixen.exe did not present a main window within 15s");
        return window!;
    }

    [Fact]
    public void MainWindow_ExposesANonEmptyName()
    {
        var window = MainWindow();
        Assert.False(
            string.IsNullOrWhiteSpace(window.Name),
            "the main window must expose a non-empty UIA Name so a screen reader can announce it");
    }

    [Fact]
    public void TerminalSurface_IsReachableAndReadable()
    {
        var window = MainWindow();

        // The terminal content must be reachable through the tree and expose a
        // text or grid pattern, or a screen reader cannot read output at all.
        var descendants = window.FindAllDescendants();
        var readable = descendants.Any(e =>
            e.Patterns.Text.IsSupported ||
            e.Patterns.Grid.IsSupported ||
            e.ControlType == ControlType.Document ||
            e.ControlType == ControlType.Text);

        Assert.True(
            readable,
            "no descendant exposes a Text/Grid pattern or Document/Text control type; " +
            "the terminal content is not readable by a screen reader");
    }

    [Fact]
    public void EveryControl_ThatIsFocusable_HasANameOrLabel()
    {
        var window = MainWindow();
        var unnamed = window.FindAllDescendants()
            .Where(e => e.Properties.IsKeyboardFocusable.ValueOrDefault)
            .Where(e => string.IsNullOrWhiteSpace(e.Name))
            .Select(e => e.ControlType.ToString())
            .ToList();

        Assert.True(
            unnamed.Count == 0,
            $"focusable elements without a UIA Name: {string.Join(", ", unnamed)}");
    }

    [Fact]
    public void AxeWindows_FindsNoErrors()
    {
        // Programmatic Axe.Windows scan of the process UIA tree - the desktop
        // analog of an axe web scan. It maps findings to accessibility rules.
        //
        // VERIFY: this uses the Axe.Windows.Automation 2.x API shape. Confirm the
        // builder methods and the result type against the installed package
        // version; the surface has changed across releases.
        var outputDir = Path.Combine(Path.GetTempPath(), "wixen-axe-" + _app.ProcessId);
        Directory.CreateDirectory(outputDir);

        var config = Axe.Windows.Automation.Config.Builder
            .ForProcessId(_app.ProcessId)
            .WithOutputDirectory(outputDir)
            .WithOutputFileFormat(Axe.Windows.Automation.OutputFileFormat.A11yTest)
            .Build();

        var scanner = Axe.Windows.Automation.ScannerFactory.CreateScanner(config);
        var output = scanner.Scan(null);

        var errorCount = output.WindowScanOutputs.Sum(w => w.ErrorCount);
        Assert.True(
            errorCount == 0,
            $"Axe.Windows reported {errorCount} accessibility error(s). " +
            $"A11yTest results written to {outputDir} - open them in Accessibility Insights.");
    }

    public void Dispose()
    {
        _automation.Dispose();
        if (!_app.HasExited)
        {
            _app.Close();
        }
        _app.Dispose();
    }
}
