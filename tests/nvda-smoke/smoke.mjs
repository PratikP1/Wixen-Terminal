// Experimental NVDA speech smoke test.
//
// Drives a real NVDA screen reader (via Guidepup) against the running wixen.exe
// and checks that NVDA speaks *something* when the terminal starts and receives
// focus. This is the closest automation gets to the one guardrail that otherwise
// needs a human: does the screen reader actually say anything useful.
//
// SCAFFOLDING and inherently flaky: NVDA automation needs a headed session and
// is timing-sensitive. This never blocks a merge. It is a starting point - the
// real assertions about *what* NVDA should say (command blocks, exit codes,
// table cells) are for you to fill in against real runs. It does not replace
// hands-on testing.

import { spawn } from "node:child_process";
import { setTimeout as sleep } from "node:timers/promises";
import { nvda } from "@guidepup/guidepup";

const exe = process.env.WIXEN_EXE;
if (!exe) {
  console.error("WIXEN_EXE is not set; build wixen first and point WIXEN_EXE at it.");
  process.exit(2);
}

let app;
try {
  app = spawn(exe, { stdio: "ignore" });
  await sleep(3000); // let the window appear and take focus

  await nvda.start();
  await sleep(2000); // let NVDA settle and announce the focused window

  // Type a command so there is output for NVDA to react to.
  await nvda.type("echo hello\n");
  await sleep(2000);

  const spoken = await nvda.spokenPhraseLog();
  console.log("NVDA spoke:\n" + spoken.map((p) => "  - " + p).join("\n"));

  if (spoken.length === 0) {
    console.error("NVDA spoke nothing - the terminal may not be exposing focus or output.");
    process.exitCode = 1;
  }
} catch (err) {
  console.error("NVDA smoke test error (expected to be flaky):", err);
  process.exitCode = 1;
} finally {
  try {
    await nvda.stop();
  } catch {}
  if (app && !app.killed) {
    app.kill();
  }
}
