-- Wixen Terminal shell integration for Clink (cmd.exe)
--
-- This script emits OSC 133 sequences that enable Wixen to identify
-- prompt boundaries, command starts, and command completions. With
-- these markers the accessibility tree can expose individual command
-- blocks, and screen readers can navigate between commands.
--
-- Install (system-wide):
--   Copy this file to %LOCALAPPDATA%\clink\wixen.lua
--
-- Install (portable):
--   Place this file next to wixen.exe; it will be auto-installed on
--   first launch when portable mode is active.

local ESC = "\027"
local OSC = ESC .. "]133;"
local ST  = "\a"

-- A  Prompt start — emitted before the prompt is displayed.
-- B  Command start — emitted after the user presses Enter.
-- C  Pre-command — emitted before command output starts.
-- D  Command complete — emitted after the command finishes (with exit code).

-- Prompt filter: wrap the prompt with A (start) marker.
-- Priority 1 ensures we run before other prompt filters.
local p = clink.promptfilter(1)
function p:filter(prompt)
    return OSC .. "A" .. ST .. prompt
end

-- After Enter is pressed, emit B (command start).
local function on_begin_edit()
    clink.print(OSC .. "B" .. ST, NONL)
end

if clink.onbeginedit then
    clink.onbeginedit(on_begin_edit)
end

-- Before command output starts, emit C (pre-command).
local function on_filter_input(line)
    clink.print(OSC .. "C" .. ST, NONL)
    return false -- don't modify the input
end

if clink.onfilterinput then
    clink.onfilterinput(on_filter_input)
end

-- After command finishes, emit D (command complete) with exit code.
local function on_end_edit(line)
    local code = 0
    if os.getlasterror then
        code = os.getlasterror()
    end
    clink.print(OSC .. "D;" .. code .. ST, NONL)
end

if clink.onendedit then
    clink.onendedit(on_end_edit)
end
