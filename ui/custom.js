// The key check, quiet by default. The engine (src/Synth.cpp checkKeys)
// always repairs what it safely can (a key pressed again while down ends the
// earlier press; a stray key is let go; a bow left on with no key is lifted)
// and posts "keys.check" when one of its rules breaks: rules that hold
// however the keys are played, which the tests run constantly without a
// break, so a report means a path no test reaches.
//
// This widget sits in the collapsed Model panel: a status line, the
// details in the browser's console (F12) for Claude, and a toast only if
// "Alert me" is ticked (remembered in this browser). Logging and a report
// on Stop all sound are to come from synthkit (docs/PLAN.md "Waiting on
// synthkit").

const RULES = [
  [1, "the bow on more than two strings"],
  [2, "the bowed strings don't match the note"],
  [4, "the bow left on with no key down (lifted)"],
  [8, "a Stop release that didn't end (lifted)"],
  [16, "a note bowed with its key up"],
  [32, "a key held twice"],
  [64, "a key the instrument held that was up (let go)"],
  [128, "more voices shown than keys down"],
  [256, "a key pressed again while still down: its release never arrived, or two sources played it (the earlier note was let go)"],
];
const NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
const noteName = (n) => `${NAMES[n % 12]}${Math.floor(n / 12) - 1}`;
const ALERT_KEY = "stringmodel.keyCheckAlerts";

function loadAlerts() {
  try {
    return localStorage.getItem(ALERT_KEY) === "1";
  } catch (e) {
    return false;  // no storage here (some plugin hosts): alerts start off
  }
}
function saveAlerts(on) {
  try {
    localStorage.setItem(ALERT_KEY, on ? "1" : "0");
  } catch (e) {}
}

export function register(sk) {
  sk.widget("keyCheck", (el, ctx) => {
    let count = 0;
    el.classList.add("key-check");
    const status = document.createElement("div");
    status.className = "key-check-status";
    status.textContent = "Key check: all clear";
    status.title = "Checks the keys against the notes while you play. It stays clear unless something goes wrong.";
    const label = document.createElement("label");
    label.className = "key-check-alerts";
    const box = document.createElement("input");
    box.type = "checkbox";
    box.checked = loadAlerts();
    box.addEventListener("change", () => saveAlerts(box.checked));
    label.append(box, " Alert me");
    el.append(status, label);
    ctx.onMessage("keys.check", (code, note, keysDown, voicesShown) => {
      count += 1;
      const what = RULES.filter(([bit]) => code & bit).map(([, text]) => text).join("; ");
      const at = note >= 0 ? ` (${noteName(note)})` : "";
      status.classList.add("is-bad");
      status.textContent = `Key check: ${count} ${count === 1 ? "report" : "reports"}. Last: ${what}${at}`;
      const details = { time: new Date().toISOString(), code, note, keysDown, voicesShown, what };
      status.title = JSON.stringify(details);
      console.warn("[key check]", details);
      if (box.checked) ctx.toast(`Key check: ${what}${at}. Details in the console (F12) for Claude.`, "error");
    });
  });
}
