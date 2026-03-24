# Wixen Terminal shell integration for PowerShell
#
# This script emits OSC 133 sequences that enable Wixen to identify
# prompt boundaries, command starts, and command completions. With
# these markers the accessibility tree can expose individual command
# blocks, and screen readers can navigate between commands.
#
# Install (per-user):
#   Add this line to your PowerShell profile ($PROFILE):
#     . "$env:APPDATA\wixen\wixen.ps1"
#
# Install (portable):
#   Place this file next to wixen.exe; it will be auto-loaded when
#   running PowerShell inside Wixen Terminal.
#
# Compatibility:
#   PowerShell 5.1+ and PowerShell 7+ (pwsh.exe)

# Only activate when running inside Wixen Terminal.
# Wixen sets WIXEN_TERMINAL=1 in the child process environment.
if ($env:WIXEN_TERMINAL -ne "1") {
    return
}

# OSC 133 escape sequences
$script:ESC = [char]0x1B
$script:BEL = [char]0x07
$script:OSC133 = "${script:ESC}]133;"

# Save the user's original prompt function (if any) so we can wrap it.
$script:OriginalPrompt = $function:prompt

# Override the prompt function to emit OSC 133 markers.
function prompt {
    # D — Command complete (with exit code from the *previous* command).
    # We emit D at the start of the prompt because that's when the
    # previous command has finished and $LASTEXITCODE is available.
    # Skip D on the very first prompt (no command has run yet).
    if ($script:WixenCommandStarted) {
        $code = if ($?) { 0 } else { if ($LASTEXITCODE) { $LASTEXITCODE } else { 1 } }
        [Console]::Write("${script:OSC133}D;${code}${script:BEL}")
    }

    # A — Prompt start
    [Console]::Write("${script:OSC133}A${script:BEL}")

    # Invoke the original prompt to get the prompt string.
    if ($script:OriginalPrompt) {
        $promptText = & $script:OriginalPrompt
    } else {
        $promptText = "PS $($executionContext.SessionState.Path.CurrentLocation)$('>' * ($nestedPromptLevel + 1)) "
    }

    # B — Command start (end of prompt, user input begins)
    # Emitted after the prompt text so screen readers see the prompt first.
    "${promptText}${script:OSC133}B${script:BEL}"
}

# Hook: emit C (pre-command) before each command executes.
# PSReadLine's AddToHistory or CommandNotFound are not reliable.
# Instead we use a PSReadLine key handler for Enter.
if (Get-Module PSReadLine -ErrorAction SilentlyContinue) {
    # Wrap the AcceptLine handler to inject OSC 133;C before execution.
    Set-PSReadLineKeyHandler -Key Enter -ScriptBlock {
        # C — Pre-command (output is about to start)
        [Console]::Write("${script:OSC133}C${script:BEL}")
        $script:WixenCommandStarted = $true
        [Microsoft.PowerShell.PSConsoleReadLine]::AcceptLine()
    }
} else {
    # Fallback without PSReadLine: emit C via a command validator.
    # This is less precise but still provides command boundaries.
    $script:WixenCommandStarted = $false
    $ExecutionContext.InvokeCommand.PreCommandLookupAction = {
        param($commandName, $eventArgs)
        if (-not $script:WixenCommandStarted) {
            [Console]::Write("${script:OSC133}C${script:BEL}")
            $script:WixenCommandStarted = $true
        }
    }
}

# Also emit OSC 7 (working directory) so Wixen can track CWD.
# Override Set-Location to emit OSC 7 on directory changes.
$script:OriginalSetLocation = Get-Command Set-Location -CommandType Cmdlet
function Set-Location {
    & $script:OriginalSetLocation @args
    $cwd = (Get-Location).Path
    $hostname = [System.Net.Dns]::GetHostName()
    $escapedPath = $cwd -replace '\\', '/'
    [Console]::Write("${script:ESC}]7;file://${hostname}/${escapedPath}${script:BEL}")
}

# Emit initial CWD on script load.
$cwd = (Get-Location).Path
$hostname = [System.Net.Dns]::GetHostName()
$escapedPath = $cwd -replace '\\', '/'
[Console]::Write("${script:ESC}]7;file://${hostname}/${escapedPath}${script:BEL}")
