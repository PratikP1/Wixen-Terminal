using System;
using System.IO;

namespace WixenA11y.Tests;

/// <summary>
/// Locates the built wixen.exe for the tests to launch.
/// </summary>
internal static class WixenExecutable
{
    /// <summary>
    /// Returns the path to wixen.exe. Prefers the WIXEN_EXE environment variable
    /// (set by CI), then falls back to the release build under the repo root.
    /// Throws with a clear message if it cannot be found, so a missing build is
    /// obvious rather than a confusing launch failure.
    /// </summary>
    public static string Locate()
    {
        var fromEnv = Environment.GetEnvironmentVariable("WIXEN_EXE");
        if (!string.IsNullOrWhiteSpace(fromEnv) && File.Exists(fromEnv))
        {
            return fromEnv;
        }

        // tests/a11y-uia/bin/<config>/<tfm>/ -> repo root is four levels up.
        var repoRoot = FindRepoRoot(AppContext.BaseDirectory);
        var candidate = Path.Combine(repoRoot, "target", "release", "wixen.exe");
        if (File.Exists(candidate))
        {
            return candidate;
        }

        throw new FileNotFoundException(
            $"wixen.exe not found. Build it first with `cargo build --release --bin wixen`, " +
            $"or set WIXEN_EXE. Looked at env WIXEN_EXE and '{candidate}'.");
    }

    private static string FindRepoRoot(string start)
    {
        var dir = new DirectoryInfo(start);
        while (dir is not null)
        {
            if (File.Exists(Path.Combine(dir.FullName, "Cargo.toml")))
            {
                return dir.FullName;
            }
            dir = dir.Parent;
        }
        // Fall back to the start directory; Locate() will then throw a clear error.
        return start;
    }
}
