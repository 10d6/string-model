// The key check: the engine (src/Synth.cpp checkKeys) posts "keys.check"
// when a rule about the keys breaks: rules that hold however the keys are
// played, which the tests run constantly without a break. So a report means
// a path no test reaches: the panel says what broke, a toast says so once,
// and the browser's console (F12) keeps the details to send to Claude.

const RULES = [
  [1, "the bow on more than two strings"],
  [2, "the bowed strings don't match the note"],
  [4, "the bow left on with no key down (lifted)"],
  [8, "a Stop release that didn't end (lifted)"],
  [16, "a note bowed with its key up"],
  [32, "a key held twice"],
  [64, "a key the instrument held that was up (let go)"],
  [128, "more voices shown than keys down"],
];
const NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
const noteName = (n) => `${NAMES[n % 12]}${Math.floor(n / 12) - 1}`;

export function register(sk) {
  sk.widget("keyCheck", (el, ctx) => {
    let count = 0;
    el.classList.add("key-check");
    el.textContent = "All clear";
    el.title = "Checks the keys against the notes while you play. It stays clear unless something goes wrong.";
    ctx.onMessage("keys.check", (code, note, keysDown, voicesShown) => {
      count += 1;
      const what = RULES.filter(([bit]) => code & bit).map(([, text]) => text).join("; ");
      const at = note >= 0 ? ` (${noteName(note)})` : "";
      el.classList.add("is-bad");
      el.textContent = `${count} ${count === 1 ? "problem" : "problems"}. Last: ${what}${at}`;
      const details = { time: new Date().toISOString(), code, note, keysDown, voicesShown, what };
      el.title = JSON.stringify(details);
      console.warn("[key check]", details);
      if (count === 1) ctx.toast(`Key check: ${what}${at}. Please tell Claude what you were playing (details in the console, F12).`, "error");
    });
  });
}
