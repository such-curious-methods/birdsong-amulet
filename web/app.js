const PROTOCOL_PREFIX = "AMULET/2 ";
const MAX_STEPS = 8;
const MIN_FREQUENCY_HZ = 262;
const MAX_FREQUENCY_HZ = 4186;
// A step at 0 Hz is a rest: silent, but its light still shows.
const REST_FREQUENCY_HZ = 0;
const FALLBACK_PITCH_HZ = 1047;
const MIN_NOTE_DURATION_MS = 100;
const MAX_NOTE_DURATION_MS = 1000;
const MAX_START_DELAY_MS = 1000;
const TIMING_STEP_MS = 10;
const DEFAULT_COLOR = "#00aa80";
const EDITOR_KEYS = ["call", "response"];
const BOOTSTRAP_KEYS = ["response", "call"];
const BROWSER_PREVIEW_GAIN = 0.08;
const BROWSER_PREVIEW_LEAD_S = 0.02;
const BROWSER_PREVIEW_ATTACK_S = 0.005;
const BROWSER_PREVIEW_RELEASE_S = 0.01;
const BrowserAudioContext = window.AudioContext || window.webkitAudioContext;
const AUDITION_SECONDS = 0.16;
const DEFAULT_SCALE = { root: 0, type: "chromatic" };

const EDITOR_DEFINITIONS = {
  call: {
    label: "call",
    pattern: [1047, 1319, 1568, 2093].map((frequency) => ({
      frequency,
      color: DEFAULT_COLOR,
    })),
    noteDurationMs: 330,
    startDelayMs: 0,
    resetPrompt: "Restore this Amulet's default call? That is the original C6 major call (cyan lights, 330 ms notes, no start delay) unless this Amulet was given its own default when it was set up.",
  },
  response: {
    label: "response",
    pattern: [523, 659, 784, 1047].map((frequency) => ({
      frequency,
      color: DEFAULT_COLOR,
    })),
    noteDurationMs: 330,
    startDelayMs: 0,
    resetPrompt: "Restore this Amulet's default response? That is the original C5 major response (cyan lights, 330 ms notes, no start delay) unless this Amulet was given its own default when it was set up.",
  },
};

const NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
const SCALES = {
  chromatic: { label: "All notes", intervals: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11] },
  major: { label: "Major", intervals: [0, 2, 4, 5, 7, 9, 11] },
  minor: { label: "Minor", intervals: [0, 2, 3, 5, 7, 8, 10] },
  "major-pentatonic": { label: "Major pentatonic", intervals: [0, 2, 4, 7, 9] },
  "minor-pentatonic": { label: "Minor pentatonic", intervals: [0, 3, 5, 7, 10] },
};
const NOTES = [];
for (let midi = 60; midi <= 108; midi += 1) {
  const octave = Math.floor(midi / 12) - 1;
  NOTES.push({
    frequency: Math.round(440 * (2 ** ((midi - 69) / 12))),
    label: `${NOTE_NAMES[midi % 12]}${octave}`,
  });
}

const elements = {
  connectButton: document.querySelector("#connectButton"),
  connectionState: document.querySelector("#connectionState"),
  connectionLabel: document.querySelector("#connectionLabel"),
  connectionFeedback: document.querySelector("#connectionFeedback"),
  browserNote: document.querySelector("#browserNote"),
  stepTemplate: document.querySelector("#stepTemplate"),
};

function editorElement(root, role) {
  const element = root.querySelector(`[data-role="${role}"]`);
  if (!element) {
    throw new Error(`Missing ${role} control.`);
  }
  return element;
}

function createEditor(key) {
  const definition = EDITOR_DEFINITIONS[key];
  const root = document.querySelector(`[data-editor="${key}"]`);
  if (!root) {
    throw new Error(`Missing ${key} editor.`);
  }
  return {
    key,
    label: definition.label,
    resetPrompt: definition.resetPrompt,
    pattern: structuredClone(definition.pattern),
    noteDurationMs: definition.noteDurationMs,
    startDelayMs: definition.startDelayMs,
    scale: { ...DEFAULT_SCALE },
    dirty: false,
    revision: 0,
    baseline: null,
    reconcileRequired: false,
    elements: {
      steps: editorElement(root, "steps"),
      scaleRoot: editorElement(root, "scale-root"),
      scaleType: editorElement(root, "scale-type"),
      audition: editorElement(root, "audition"),
      octaveDownButton: editorElement(root, "octave-down"),
      octaveUpButton: editorElement(root, "octave-up"),
      noteDuration: editorElement(root, "note-duration"),
      noteDurationValue: editorElement(root, "note-duration-value"),
      startDelay: editorElement(root, "start-delay"),
      startDelayValue: editorElement(root, "start-delay-value"),
      addStepCell: editorElement(root, "add-step-cell"),
      addStepButton: editorElement(root, "add-step"),
      paintAllColor: editorElement(root, "paint-all-color"),
      paintAllButton: editorElement(root, "paint-all"),
      durationLabel: editorElement(root, "duration"),
      browserPreviewButton: editorElement(root, "browser-preview"),
      previewButton: editorElement(root, "preview"),
      saveButton: editorElement(root, "save"),
      loadButton: editorElement(root, "load"),
      resetButton: editorElement(root, "reset"),
      feedback: editorElement(root, "feedback"),
    },
  };
}

const editors = Object.fromEntries(EDITOR_KEYS.map((key) => [key, createEditor(key)]));
let activeConnection = null;
let connectBusy = false;
let audioContext = null;
let currentPlayback = null;

function setConnectionState(state, label) {
  elements.connectionState.dataset.state = state;
  elements.connectionLabel.textContent = label;
}

function setFeedback(editor, message, kind = "quiet") {
  editor.elements.feedback.textContent = message;
  editor.elements.feedback.dataset.kind = kind;
}

function setConnectionFeedback(message, kind = "quiet") {
  elements.connectionFeedback.textContent = message;
  elements.connectionFeedback.dataset.kind = kind;
}

function setDeviceActionsEnabled(enabled) {
  document.querySelectorAll(".device-action").forEach((button) => {
    button.disabled = !enabled;
  });
}

function refreshDeviceActions(connection = activeConnection) {
  setDeviceActionsEnabled(Boolean(
    connection &&
    connection === activeConnection &&
    connection.configurationsReceived.size === EDITOR_KEYS.length &&
    !connection.pendingOperation &&
    !connection.bootstrapActive &&
    !connection.closing
  ));
}

function closestNoteIndex(frequency) {
  let best = 0;
  NOTES.forEach((note, index) => {
    if (Math.abs(note.frequency - frequency) < Math.abs(NOTES[best].frequency - frequency)) {
      best = index;
    }
  });
  return best;
}

// NOTES starts at C4, so a note's index modulo 12 is its pitch class.
function inScale(editor, noteIndex) {
  const { root, type } = editor.scale;
  return SCALES[type].intervals.includes((noteIndex - root + 12) % 12);
}

function snapToScale(editor, noteIndex) {
  for (let distance = 0; distance < NOTES.length; distance += 1) {
    for (const candidate of [noteIndex + distance, noteIndex - distance]) {
      if (candidate >= 0 && candidate < NOTES.length && inScale(editor, candidate)) {
        return candidate;
      }
    }
  }
  return noteIndex;
}

// The next in-scale note strictly above (direction 1) or below (-1)
// the frequency, or -1 at the edge of the range.
function neighborNoteIndex(editor, frequency, direction) {
  const order = NOTES.map((_, index) => index);
  if (direction < 0) {
    order.reverse();
  }
  return order.find((index) => inScale(editor, index) && (
    direction > 0 ? NOTES[index].frequency > frequency : NOTES[index].frequency < frequency
  )) ?? -1;
}

function formatSeconds(milliseconds) {
  if (milliseconds === 0) {
    return "0 s";
  }
  return `${(milliseconds / 1000).toFixed(2).replace(/0+$/, "").replace(/\.$/, "")} s`;
}

function formatConfiguration(value) {
  const pattern = value.pattern.map((step) => (
    `${step.frequency}:${step.color.slice(1).toUpperCase()}`
  )).join(",");
  return `${value.noteDurationMs}:${value.startDelayMs} ${pattern}`;
}

function markEditorChanged(editor, message) {
  editor.revision += 1;
  editor.dirty = editor.reconcileRequired ||
    editor.baseline === null ||
    formatConfiguration(editor) !== editor.baseline;
  if (message) {
    setFeedback(editor, message);
  }
}

function renderTiming(editor) {
  editor.elements.noteDuration.value = String(editor.noteDurationMs);
  editor.elements.startDelay.value = String(editor.startDelayMs);
  editor.elements.noteDurationValue.value = formatSeconds(editor.noteDurationMs);
  editor.elements.startDelayValue.value = formatSeconds(editor.startDelayMs);
  const totalDurationMs = editor.startDelayMs + (editor.pattern.length * editor.noteDurationMs);
  const rests = editor.pattern.filter(isRest).length;
  const notes = editor.pattern.length - rests;
  const counts = [`${notes} ${notes === 1 ? "note" : "notes"}`];
  if (rests > 0) {
    counts.push(`${rests} ${rests === 1 ? "rest" : "rests"}`);
  }
  editor.elements.durationLabel.textContent = `${counts.join(", ")} / ${formatSeconds(totalDurationMs)} total`;
}

function moveStep(editor, from, to) {
  const [step] = editor.pattern.splice(from, 1);
  editor.pattern.splice(to, 0, step);
  renderPattern(editor);
  markEditorChanged(editor, "Order changed. Preview it before saving.");
}

function stepCard(editor, index) {
  return editor.elements.steps.children[index];
}

function isRest(step) {
  return step.frequency === REST_FREQUENCY_HZ;
}

// The pitch to return to when a rest becomes a note again, or to build on:
// the nearest earlier note, else the nearest later one.
function nearestPitch(editor, index) {
  const before = editor.pattern.slice(0, index).reverse();
  const after = editor.pattern.slice(index + 1);
  return [...before, ...after].find((step) => !isRest(step))?.frequency ?? FALLBACK_PITCH_HZ;
}

function noteLabel(frequency) {
  if (frequency === REST_FREQUENCY_HZ) {
    return "Rest";
  }
  const note = NOTES[closestNoteIndex(frequency)];
  return note.frequency === frequency ? note.label : `${frequency} Hz`;
}

// Refreshes one step's pitch controls in place, so a slider drag or nudge
// never rebuilds the strip under the pointer or keyboard focus.
function updateStepView(editor, index) {
  const card = stepCard(editor, index);
  const step = editor.pattern[index];
  const rest = isRest(step);
  card.classList.toggle("rest", rest);
  card.querySelector(".rest-toggle").setAttribute("aria-pressed", String(rest));
  card.querySelector(".pitch-slider").disabled = rest;
  card.querySelector(".note-select").disabled = rest;
  if (rest) {
    showRest(editor, card);
    return;
  }
  const noteIndex = closestNoteIndex(step.frequency);
  const exact = NOTES[noteIndex].frequency === step.frequency;
  const offScale = !exact || !inScale(editor, noteIndex);
  const label = noteLabel(step.frequency);
  const slider = card.querySelector(".pitch-slider");
  const noteSelect = card.querySelector(".note-select");

  slider.value = String(noteIndex);
  card.style.setProperty("--pitch", String(noteIndex / (NOTES.length - 1)));
  slider.setAttribute("aria-valuetext", offScale ? `${label}, outside the scale` : label);
  noteSelect.querySelector(".custom-note")?.remove();
  if (!exact) {
    const customOption = document.createElement("option");
    customOption.className = "custom-note";
    customOption.value = String(step.frequency);
    customOption.textContent = label;
    noteSelect.append(customOption);
  }
  noteSelect.value = String(step.frequency);
  card.querySelector(".frequency").textContent = `${step.frequency} Hz`;
  card.querySelector(".nudge-up").disabled = neighborNoteIndex(editor, step.frequency, 1) < 0;
  card.querySelector(".nudge-down").disabled = neighborNoteIndex(editor, step.frequency, -1) < 0;
  card.classList.toggle("off-scale", offScale);
  refreshOctaveButtons(editor);
}

function showRest(editor, card) {
  const noteSelect = card.querySelector(".note-select");
  noteSelect.querySelector(".custom-note")?.remove();
  noteSelect.append(new Option("Rest", String(REST_FREQUENCY_HZ)));
  noteSelect.lastElementChild.className = "custom-note";
  noteSelect.value = String(REST_FREQUENCY_HZ);
  card.querySelector(".pitch-slider").setAttribute("aria-valuetext", "Rest");
  card.querySelector(".frequency").textContent = "Silent";
  card.querySelector(".nudge-up").disabled = true;
  card.querySelector(".nudge-down").disabled = true;
  card.classList.remove("off-scale");
  refreshOctaveButtons(editor);
}

function toggleRest(editor, index) {
  const step = editor.pattern[index];
  if (isRest(step)) {
    const pitch = step.pitchBeforeRest ?? nearestPitch(editor, index);
    delete step.pitchBeforeRest;
    setStepFrequency(editor, index, pitch);
    setFeedback(editor, `Step ${index + 1} plays ${noteLabel(pitch)} again.`);
  } else {
    step.pitchBeforeRest = step.frequency;
    setStepFrequency(editor, index, REST_FREQUENCY_HZ);
    setFeedback(editor, `Step ${index + 1} is now a rest: silent, with its light still showing.`);
  }
  renderTiming(editor);
}

// One octave away: 12 list steps for listed notes, so rounding never drifts,
// or double/half for a custom frequency. Null when it would leave C4-C8.
function octaveShiftedFrequency(frequency, direction) {
  if (frequency === REST_FREQUENCY_HZ) {
    return REST_FREQUENCY_HZ;
  }
  const noteIndex = closestNoteIndex(frequency);
  if (NOTES[noteIndex].frequency === frequency) {
    return NOTES[noteIndex + (12 * direction)]?.frequency ?? null;
  }
  const shifted = Math.round(direction > 0 ? frequency * 2 : frequency / 2);
  return shifted >= MIN_FREQUENCY_HZ && shifted <= MAX_FREQUENCY_HZ ? shifted : null;
}

function canShiftOctave(editor, direction) {
  return editor.pattern.every((step) => octaveShiftedFrequency(step.frequency, direction) !== null);
}

function refreshOctaveButtons(editor) {
  [[editor.elements.octaveUpButton, 1, "highest"], [editor.elements.octaveDownButton, -1, "lowest"]]
    .forEach(([button, direction, extreme]) => {
      button.disabled = !canShiftOctave(editor, direction);
      button.title = button.disabled
        ? `The ${extreme} note has no room to move a whole octave.`
        : `Move every note ${direction > 0 ? "up" : "down"} an octave`;
    });
}

// Moves every step together, so the melody keeps its shape; refuses
// rather than clamping when any note would leave the range.
function shiftOctave(editor, direction) {
  if (!canShiftOctave(editor, direction)) {
    return;
  }
  editor.pattern.forEach((step, index) => {
    step.frequency = octaveShiftedFrequency(step.frequency, direction);
    // A rest's remembered pitch moves with the melody, or is dropped if it
    // would leave the range; turning the rest off then uses a nearby note.
    if (step.pitchBeforeRest !== undefined) {
      const shifted = octaveShiftedFrequency(step.pitchBeforeRest, direction);
      if (shifted === null) {
        delete step.pitchBeforeRest;
      } else {
        step.pitchBeforeRest = shifted;
      }
    }
    updateStepView(editor, index);
  });
  auditionNote(editor, editor.pattern.find((step) => !isRest(step))?.frequency ?? REST_FREQUENCY_HZ);
  markEditorChanged(editor, `Moved the whole ${editor.label} ${direction > 0 ? "up" : "down"} an octave. Preview it before saving.`);
}

function setStepFrequency(editor, index, frequency) {
  if (editor.pattern[index].frequency === frequency) {
    return;
  }
  editor.pattern[index].frequency = frequency;
  updateStepView(editor, index);
  auditionNote(editor, frequency);
  markEditorChanged(editor);
}

function nudgeStep(editor, index, direction) {
  if (isRest(editor.pattern[index])) {
    return;
  }
  const neighbor = neighborNoteIndex(editor, editor.pattern[index].frequency, direction);
  if (neighbor >= 0) {
    setStepFrequency(editor, index, NOTES[neighbor].frequency);
    setFeedback(editor, `Step ${index + 1} is now ${NOTES[neighbor].label}. Preview it before saving.`);
  }
}

const NUDGE_KEYS = { ArrowUp: 1, ArrowRight: 1, ArrowDown: -1, ArrowLeft: -1 };
const KNOB_GRAB_PX = 14;
const KNOB_HEIGHT_PX = 14; // Matches --knob-height in styles.css.

// Maps between note indexes and vertical positions on a pitch track.
function sliderGeometry(track) {
  const rect = track.getBoundingClientRect();
  const max = NOTES.length - 1;
  const travel = rect.height - KNOB_HEIGHT_PX;
  const bottom = rect.bottom - (KNOB_HEIGHT_PX / 2);
  return {
    pxPerNote: travel / max,
    yForIndex: (noteIndex) => bottom - ((noteIndex / max) * travel),
    indexForY: (y) => Math.min(max, Math.max(0, Math.round(((bottom - y) / travel) * max))),
  };
}

// The track handles pointer input itself, so a press near the knob grabs it
// (and plays its note) instead of jumping to the exact pixel under the pointer.
function bindPitchTrack(editor, index, track, slider) {
  let drag = null;
  track.addEventListener("pointerdown", (event) => {
    if (event.button !== 0 || isRest(editor.pattern[index])) {
      return;
    }
    event.preventDefault();
    slider.focus();
    track.setPointerCapture(event.pointerId);
    const geometry = sliderGeometry(track);
    const before = editor.pattern[index].frequency;
    let startIndex = Number(slider.value);
    if (Math.abs(event.clientY - geometry.yForIndex(startIndex)) <= KNOB_GRAB_PX) {
      auditionNote(editor, before);
    } else {
      startIndex = snapToScale(editor, geometry.indexForY(event.clientY));
      if (NOTES[startIndex].frequency === before) {
        auditionNote(editor, before);
      } else {
        setStepFrequency(editor, index, NOTES[startIndex].frequency);
      }
    }
    drag = { pointerId: event.pointerId, startY: event.clientY, startIndex, before, pxPerNote: geometry.pxPerNote };
    track.classList.add("dragging");
  });
  track.addEventListener("pointermove", (event) => {
    if (!drag || event.pointerId !== drag.pointerId) {
      return;
    }
    const moved = Math.round((drag.startY - event.clientY) / drag.pxPerNote);
    const target = Math.min(NOTES.length - 1, Math.max(0, drag.startIndex + moved));
    setStepFrequency(editor, index, NOTES[snapToScale(editor, target)].frequency);
  });
  const endDrag = (event) => {
    if (!drag || event.pointerId !== drag.pointerId) {
      return;
    }
    if (editor.pattern[index].frequency !== drag.before) {
      setFeedback(editor, `Step ${index + 1} is now ${noteLabel(editor.pattern[index].frequency)}. Preview it before saving.`);
    }
    drag = null;
    track.classList.remove("dragging");
  };
  track.addEventListener("pointerup", endDrag);
  track.addEventListener("pointercancel", endDrag);
}

function renderPattern(editor) {
  editor.elements.steps.replaceChildren();
  editor.pattern.forEach((step, index) => {
    const fragment = elements.stepTemplate.content.cloneNode(true);
    const card = fragment.querySelector(".tone-card");
    const number = fragment.querySelector(".step-number");
    const nudgeUp = fragment.querySelector(".nudge-up");
    const nudgeDown = fragment.querySelector(".nudge-down");
    const pitchTrack = fragment.querySelector(".pitch-track");
    const slider = fragment.querySelector(".pitch-slider");
    const noteSelect = fragment.querySelector(".note-select");
    const colorInput = fragment.querySelector(".color-input");
    const colorValue = fragment.querySelector(".color-value");
    const moveUp = fragment.querySelector(".move-up");
    const moveDown = fragment.querySelector(".move-down");
    const remove = fragment.querySelector(".remove-button");
    const stepName = `step ${index + 1}`;

    number.textContent = String(index + 1).padStart(2, "0");
    nudgeUp.setAttribute("aria-label", `Raise ${stepName}`);
    nudgeDown.setAttribute("aria-label", `Lower ${stepName}`);
    slider.max = String(NOTES.length - 1);
    slider.setAttribute("aria-label", `Pitch of ${stepName}`);
    noteSelect.setAttribute("aria-label", `Note for ${stepName}`);
    colorInput.setAttribute("aria-label", `Light color for ${stepName}`);
    // Highest notes first, matching the slider's direction.
    [...NOTES].reverse().forEach((note) => {
      const option = document.createElement("option");
      option.value = String(note.frequency);
      option.textContent = note.label;
      noteSelect.append(option);
    });
    colorInput.value = step.color;
    colorValue.textContent = step.color.toUpperCase();
    card.style.setProperty("--step-color", step.color);

    nudgeUp.addEventListener("click", () => nudgeStep(editor, index, 1));
    nudgeDown.addEventListener("click", () => nudgeStep(editor, index, -1));
    bindPitchTrack(editor, index, pitchTrack, slider);
    const restToggle = fragment.querySelector(".rest-toggle");
    restToggle.setAttribute("aria-label", `Rest for ${stepName}`);
    restToggle.addEventListener("click", () => toggleRest(editor, index));
    // Keyboard keys such as Home, End and Page Up still move the native slider.
    slider.addEventListener("input", () => {
      const snapped = snapToScale(editor, Number(slider.value));
      slider.value = String(snapped);
      setStepFrequency(editor, index, NOTES[snapped].frequency);
    });
    slider.addEventListener("change", () => {
      setFeedback(editor, `Step ${index + 1} is now ${noteLabel(editor.pattern[index].frequency)}. Preview it before saving.`);
    });
    // Arrow keys move one scale note, not one semitone that snapping would undo.
    slider.addEventListener("keydown", (event) => {
      const direction = NUDGE_KEYS[event.key];
      if (direction) {
        event.preventDefault();
        nudgeStep(editor, index, direction);
      }
    });
    noteSelect.addEventListener("change", () => {
      setStepFrequency(editor, index, Number(noteSelect.value));
      setFeedback(editor, "Pattern changed. Preview it when the Amulet is connected.");
    });
    colorInput.addEventListener("input", () => {
      editor.pattern[index].color = colorInput.value;
      colorValue.textContent = colorInput.value.toUpperCase();
      card.style.setProperty("--step-color", colorInput.value);
      markEditorChanged(editor);
    });
    colorInput.addEventListener("change", () => {
      setFeedback(editor, "Color changed. Preview is temporary until you save.");
    });
    moveUp.disabled = index === 0;
    moveDown.disabled = index === editor.pattern.length - 1;
    moveUp.addEventListener("click", () => moveStep(editor, index, index - 1));
    moveDown.addEventListener("click", () => moveStep(editor, index, index + 1));
    remove.disabled = editor.pattern.length === 1;
    remove.addEventListener("click", () => {
      editor.pattern.splice(index, 1);
      renderPattern(editor);
      markEditorChanged(editor, "Note removed. The saved Amulet pattern is unchanged.");
    });

    editor.elements.steps.append(fragment);
    updateStepView(editor, index);
  });

  // The add cell always follows the last note, and disappears at the limit.
  editor.elements.steps.append(editor.elements.addStepCell);
  editor.elements.addStepCell.hidden = editor.pattern.length >= MAX_STEPS;
  editor.elements.addStepButton.disabled = editor.pattern.length >= MAX_STEPS;
  renderTiming(editor);
}

function renderScale(editor) {
  editor.elements.scaleRoot.value = String(editor.scale.root);
  editor.elements.scaleType.value = editor.scale.type;
  editor.elements.scaleRoot.disabled = editor.scale.type === "chromatic";
  editor.pattern.forEach((_, index) => updateStepView(editor, index));
}

function highlightStep(editor, activeIndex) {
  editor.elements.steps.querySelectorAll(".tone-card").forEach((card, index) => {
    card.classList.toggle("playing", index === activeIndex);
  });
}

function finishBrowserPreview(editor, message) {
  highlightStep(editor, -1);
  editor.elements.browserPreviewButton.textContent = "Play in browser";
  setFeedback(editor, message);
}

// Mirrors the firmware: wait startDelayMs, then step one 50% square wave
// through the notes back to back, changing only its frequency.
async function playInBrowser(editor) {
  audioContext ??= new BrowserAudioContext();
  if (audioContext.state === "suspended") {
    await audioContext.resume();
  }
  const stepSeconds = editor.noteDurationMs / 1000;
  const start = audioContext.currentTime + BROWSER_PREVIEW_LEAD_S + (editor.startDelayMs / 1000);
  const end = start + (editor.pattern.length * stepSeconds);
  const oscillator = audioContext.createOscillator();
  const gain = audioContext.createGain();
  oscillator.type = "square";
  // Rests fade the level to silence; notes change only the frequency, so
  // back-to-back notes stay gap-free like the buzzer.
  let level = 0;
  gain.gain.setValueAtTime(0, start);
  editor.pattern.forEach((step, index) => {
    const stepStart = start + (index * stepSeconds);
    const target = isRest(step) ? 0 : BROWSER_PREVIEW_GAIN;
    if (!isRest(step)) {
      oscillator.frequency.setValueAtTime(step.frequency, stepStart);
    }
    if (target !== level) {
      gain.gain.setValueAtTime(level, stepStart);
      gain.gain.linearRampToValueAtTime(
        target,
        stepStart + (target ? BROWSER_PREVIEW_ATTACK_S : BROWSER_PREVIEW_RELEASE_S)
      );
      level = target;
    }
  });
  if (level) {
    gain.gain.setValueAtTime(level, end - BROWSER_PREVIEW_RELEASE_S);
    gain.gain.linearRampToValueAtTime(0, end);
  }
  oscillator.connect(gain).connect(audioContext.destination);

  const playback = { editor, oscillator, gain, timeouts: [] };
  const firstNoteMs = (start - audioContext.currentTime) * 1000;
  editor.pattern.forEach((_, index) => {
    playback.timeouts.push(window.setTimeout(
      () => highlightStep(editor, index),
      firstNoteMs + (index * editor.noteDurationMs)
    ));
  });
  oscillator.onended = () => {
    gain.disconnect();
    if (currentPlayback === playback) {
      currentPlayback = null;
      finishBrowserPreview(editor, `Finished playing the ${editor.label} in your browser.`);
    }
  };
  oscillator.start(start);
  oscillator.stop(end);
  currentPlayback = playback;
  editor.elements.browserPreviewButton.textContent = "Stop";
  setFeedback(editor, "Playing in your browser...");
}

function stopBrowserPreview() {
  const playback = currentPlayback;
  if (!playback) {
    return;
  }
  currentPlayback = null;
  playback.timeouts.forEach((timeout) => window.clearTimeout(timeout));
  const now = audioContext.currentTime;
  const level = playback.gain.gain;
  level.cancelScheduledValues(now);
  level.setValueAtTime(level.value, now);
  level.linearRampToValueAtTime(0, now + BROWSER_PREVIEW_RELEASE_S);
  playback.oscillator.stop(now + BROWSER_PREVIEW_RELEASE_S);
  finishBrowserPreview(playback.editor, "Stopped playing in your browser.");
}

let auditionOscillator = null;

// A short blip of one note while editing. Skipped while a melody plays.
function auditionNote(editor, frequency) {
  if (frequency === REST_FREQUENCY_HZ ||
      !editor.elements.audition.checked || currentPlayback || !BrowserAudioContext) {
    return;
  }
  audioContext ??= new BrowserAudioContext();
  if (audioContext.state === "suspended") {
    void audioContext.resume();
  }
  auditionOscillator?.stop();
  const start = audioContext.currentTime + BROWSER_PREVIEW_LEAD_S;
  const end = start + AUDITION_SECONDS;
  const oscillator = audioContext.createOscillator();
  const gain = audioContext.createGain();
  oscillator.type = "square";
  oscillator.frequency.setValueAtTime(frequency, start);
  gain.gain.setValueAtTime(0, start);
  gain.gain.linearRampToValueAtTime(BROWSER_PREVIEW_GAIN, start + BROWSER_PREVIEW_ATTACK_S);
  gain.gain.setValueAtTime(BROWSER_PREVIEW_GAIN, end - BROWSER_PREVIEW_RELEASE_S);
  gain.gain.linearRampToValueAtTime(0, end);
  oscillator.connect(gain).connect(audioContext.destination);
  oscillator.onended = () => {
    gain.disconnect();
    if (auditionOscillator === oscillator) {
      auditionOscillator = null;
    }
  };
  oscillator.start(start);
  oscillator.stop(end);
  auditionOscillator = oscillator;
}

function parsePattern(encoded) {
  const parsed = encoded.split(",").map((token) => {
    const match = token.match(/^(\d+):([0-9a-f]{6})$/i);
    if (!match) {
      throw new Error("The Amulet returned a malformed pattern.");
    }
    const frequency = Number(match[1]);
    if (!Number.isSafeInteger(frequency) ||
        (frequency !== REST_FREQUENCY_HZ &&
          (frequency < MIN_FREQUENCY_HZ || frequency > MAX_FREQUENCY_HZ))) {
      throw new Error("The Amulet returned an unsupported note frequency.");
    }
    return {
      frequency,
      color: `#${match[2].toLowerCase()}`,
    };
  });
  if (parsed.length < 1 || parsed.length > MAX_STEPS) {
    throw new Error("The Amulet returned an unsupported number of steps.");
  }
  return parsed;
}

function parseConfiguration(encoded) {
  const match = encoded.match(/^(\d+):(\d+) (.+)$/);
  if (!match) {
    throw new Error("The Amulet returned malformed timing settings.");
  }

  const noteDurationMs = Number(match[1]);
  const startDelayMs = Number(match[2]);
  if (noteDurationMs < MIN_NOTE_DURATION_MS ||
      noteDurationMs > MAX_NOTE_DURATION_MS ||
      startDelayMs < 0 ||
      startDelayMs > MAX_START_DELAY_MS ||
      noteDurationMs % TIMING_STEP_MS !== 0 ||
      startDelayMs % TIMING_STEP_MS !== 0) {
    throw new Error("The Amulet returned unsupported timing settings.");
  }

  return {
    pattern: parsePattern(match[3]),
    noteDurationMs,
    startDelayMs,
  };
}

function commandFor(editor, verb, payload = "") {
  const target = editor.key === "call" ? " C" : "";
  return `${verb}${target}${payload ? ` ${payload}` : ""}`;
}

async function writeCommand(connection, command) {
  const previousWrite = connection.writeChain.catch(() => undefined);
  const nextWrite = previousWrite.then(async () => {
    if (connection !== activeConnection || connection.closing || !connection.port.writable) {
      throw new Error("The Amulet is not connected.");
    }
    const writer = connection.port.writable.getWriter();
    try {
      await writer.write(new TextEncoder().encode(`${PROTOCOL_PREFIX}${command}\n`));
    } finally {
      writer.releaseLock();
    }
  });
  connection.writeChain = nextWrite;
  return nextWrite;
}

function finishOperation(connection) {
  if (connection.operationTimer) {
    window.clearTimeout(connection.operationTimer);
    connection.operationTimer = null;
  }
  connection.pendingOperation = null;
  refreshDeviceActions(connection);
}

function interruptOperation(connection) {
  const operation = connection.pendingOperation;
  if (operation && operation.name !== "preview") {
    const editor = editors[operation.target];
    editor.reconcileRequired = true;
    editor.dirty = true;
  }
  finishOperation(connection);
}

function beginOperation(connection, editor, name, payload) {
  if (connection.pendingOperation || connection.bootstrapActive) {
    return false;
  }
  const operation = {
    name,
    target: editor.key,
    revision: editor.revision,
    payload,
  };
  connection.pendingOperation = operation;
  refreshDeviceActions(connection);
  connection.operationTimer = window.setTimeout(() => {
    if (connection.pendingOperation === operation) {
      void closeConnection(
        connection,
        "Response timed out",
        "The Amulet did not answer. Reconnect before trying another command.",
        "error"
      );
    }
  }, 4000);
  return true;
}

async function runOperation(editor, name, command, message, payload = null) {
  const connection = activeConnection;
  if (!connection || !beginOperation(connection, editor, name, payload)) {
    return;
  }
  setFeedback(editor, message, "working");
  try {
    await writeCommand(connection, command);
  } catch (error) {
    interruptOperation(connection);
    setFeedback(editor, error.message, "error");
  }
}

function clearBootstrap(connection) {
  if (connection.bootstrapTimer) {
    window.clearTimeout(connection.bootstrapTimer);
    connection.bootstrapTimer = null;
  }
  connection.bootstrapActive = false;
  connection.bootstrapTarget = null;
}

async function requestBootstrapTarget(connection, key) {
  if (connection !== activeConnection || connection.closing || !connection.bootstrapActive) {
    return;
  }
  connection.bootstrapTarget = key;
  try {
    await writeCommand(connection, commandFor(editors[key], "GET"));
  } catch (error) {
    if (connection === activeConnection) {
      void closeConnection(connection, "Connection failed", error.message, "error");
    }
  }
}

function finishBootstrap(connection) {
  clearBootstrap(connection);
  setConnectionState("connected", "Amulet connected");
  if (connection.preservedEditors.size > 0) {
    setConnectionFeedback("Loaded device settings; your unsaved editor changes stayed in place.", "success");
  } else {
    setConnectionFeedback("Loaded the saved call and response.", "success");
  }
  refreshDeviceActions(connection);
}

async function startBootstrap(connection, initialDelayMs = 0) {
  if (initialDelayMs > 0) {
    await delay(initialDelayMs);
  }
  if (connection !== activeConnection ||
      connection.closing ||
      connection.bootstrapActive ||
      connection.pendingOperation ||
      connection.configurationsReceived.size === EDITOR_KEYS.length) {
    return;
  }

  connection.configurationsReceived.clear();
  connection.preservedEditors.clear();
  connection.bootstrapActive = true;
  refreshDeviceActions(connection);
  connection.bootstrapTimer = window.setTimeout(() => {
    if (connection.bootstrapActive && connection === activeConnection) {
      void closeConnection(
        connection,
        "Firmware did not answer",
        "The Amulet did not return both call and response settings. Flash the current firmware, then reconnect.",
        "error"
      );
    }
  }, 4000);
  await requestBootstrapTarget(connection, BOOTSTRAP_KEYS[0]);
}

function applyConfiguration(connection, editor, encoded) {
  const operation = connection.pendingOperation;
  const bootstrap = connection.bootstrapActive && connection.bootstrapTarget === editor.key;
  const explicitOperation = operation?.target === editor.key &&
    (operation.name === "load" || operation.name === "reset");
  if (!bootstrap && !explicitOperation) {
    return;
  }

  try {
    const received = parseConfiguration(encoded);
    const baseline = formatConfiguration(received);
    const replaceEditor = explicitOperation
      ? operation.revision === editor.revision
      : !editor.dirty && !editor.reconcileRequired;
    editor.baseline = baseline;

    if (replaceEditor) {
      editor.pattern = received.pattern;
      editor.noteDurationMs = received.noteDurationMs;
      editor.startDelayMs = received.startDelayMs;
      editor.dirty = false;
      editor.revision += 1;
      renderPattern(editor);
    } else {
      editor.dirty = formatConfiguration(editor) !== editor.baseline;
    }
    editor.reconcileRequired = false;

    if (bootstrap) {
      connection.configurationsReceived.add(editor.key);
      connection.bootstrapTarget = null;
      if (!replaceEditor) {
        connection.preservedEditors.add(editor.key);
        setFeedback(editor, "Connected. Your unsaved composition stayed in the editor; Save will put it on this Amulet.", "success");
      } else {
        setFeedback(editor, `Loaded the ${editor.label} stored on this Amulet.`, "success");
      }

      const nextKey = BOOTSTRAP_KEYS.find((key) => !connection.configurationsReceived.has(key));
      if (nextKey) {
        void requestBootstrapTarget(connection, nextKey);
      } else {
        finishBootstrap(connection);
      }
      return;
    }

    finishOperation(connection);
    if (!replaceEditor) {
      setFeedback(editor, "Newer edits stayed in the editor; the device settings were not allowed to overwrite them.", "success");
    } else if (operation.name === "reset") {
      setFeedback(editor, `Restored the factory ${editor.label}.`, "success");
    } else {
      setFeedback(editor, `Loaded the ${editor.label} currently saved on this Amulet.`, "success");
    }
  } catch (error) {
    if (bootstrap) {
      void closeConnection(connection, "Invalid firmware response", error.message, "error");
    } else {
      finishOperation(connection);
      setFeedback(editor, error.message, "error");
    }
  }
}

function handleAcknowledgement(connection, editor, name) {
  const operation = connection.pendingOperation;
  if (operation?.target !== editor.key || operation.name !== name) {
    return;
  }

  if (name === "save") {
    editor.baseline = operation.payload;
    editor.reconcileRequired = false;
    editor.dirty = formatConfiguration(editor) !== editor.baseline;
  }
  finishOperation(connection);

  if (name === "preview") {
    const suffix = editor.key === "call"
      ? " Nothing was saved or broadcast."
      : " Nothing was saved.";
    setFeedback(editor, `Preview queued on the Amulet.${suffix}`, "success");
  } else if (editor.dirty) {
    setFeedback(editor, `Saved the ${editor.label} that was sent; newer edits are still unsaved.`, "success");
  } else {
    setFeedback(editor, `Saved. This Amulet will use the new ${editor.label} after every reboot.`, "success");
  }
}

function handleProtocolLine(connection, line) {
  if (connection !== activeConnection || connection.closing) {
    return;
  }

  const response = line.slice(PROTOCOL_PREFIX.length);
  if (response === "READY" || response.startsWith("READY ")) {
    if (connection.bootstrapActive) {
      clearBootstrap(connection);
      connection.configurationsReceived.clear();
      setConnectionState("working", "Amulet restarted");
      setConnectionFeedback("The Amulet restarted. Reloading both saved patterns...", "working");
      void startBootstrap(connection);
      return;
    }
    if (connection.configurationsReceived.size > 0 || connection.pendingOperation) {
      interruptOperation(connection);
      connection.configurationsReceived.clear();
      setConnectionState("working", "Amulet restarted");
      setConnectionFeedback("The Amulet restarted. Reloading both saved patterns...", "working");
    }
    void startBootstrap(connection);
    return;
  }
  if (response.startsWith("CALL_CONFIG ")) {
    applyConfiguration(connection, editors.call, response.slice(12));
    return;
  }
  if (response.startsWith("CONFIG ")) {
    applyConfiguration(connection, editors.response, response.slice(7));
    return;
  }
  if (response === "OK CALL_PREVIEWING") {
    handleAcknowledgement(connection, editors.call, "preview");
    return;
  }
  if (response === "OK PREVIEWING") {
    handleAcknowledgement(connection, editors.response, "preview");
    return;
  }
  if (response === "OK CALL_SAVED") {
    handleAcknowledgement(connection, editors.call, "save");
    return;
  }
  if (response === "OK SAVED") {
    handleAcknowledgement(connection, editors.response, "save");
    return;
  }
  if (response.startsWith("ERROR ")) {
    const code = response.slice(6);
    if (connection.bootstrapActive) {
      const oldFirmware = connection.bootstrapTarget === "call" && code === "UNKNOWN_COMMAND";
      void closeConnection(
        connection,
        oldFirmware ? "Firmware update required" : "Could not load settings",
        oldFirmware
          ? "This firmware can configure responses but not outgoing calls. Flash the current firmware, then reconnect."
          : `The Amulet reported: ${code.replaceAll("_", " ").toLowerCase()}.`,
        "error"
      );
      return;
    }

    const operation = connection.pendingOperation;
    if (!operation) {
      return;
    }
    const editor = editors[operation.target];
    finishOperation(connection);
    if (code === "INVALID_PATTERN" && editor.pattern.some(isRest)) {
      setFeedback(editor, "This Amulet's firmware is too old to play rests. Flash the current firmware, or turn the rests back into notes.", "error");
      return;
    }
    setFeedback(editor, `The Amulet reported: ${code.replaceAll("_", " ").toLowerCase()}.`, "error");
  }
}

function processReceivedText(connection, text) {
  connection.receiveBuffer += text;
  const lines = connection.receiveBuffer.split(/\r\n|\n|\r/);
  connection.receiveBuffer = lines.pop() ?? "";
  lines.forEach((line) => {
    if (line.startsWith(PROTOCOL_PREFIX)) {
      handleProtocolLine(connection, line);
    } else if (/^AMULET\/\d+ /.test(line) && !connection.closing) {
      void closeConnection(
        connection,
        "Firmware update required",
        "This page needs the current Amulet configuration protocol.",
        "error"
      );
    }
  });
}

async function readSerial(connection) {
  const decoder = new TextDecoder();
  while (connection === activeConnection && connection.port.readable && !connection.closing) {
    const currentReader = connection.port.readable.getReader();
    connection.reader = currentReader;
    try {
      while (true) {
        const { value, done } = await currentReader.read();
        if (done) {
          break;
        }
        processReceivedText(connection, decoder.decode(value, { stream: true }));
      }
    } catch (error) {
      if (!connection.closing && connection === activeConnection) {
        setConnectionFeedback(`Serial read recovered after: ${error.message}`, "error");
        await delay(100);
      }
    } finally {
      currentReader.releaseLock();
      if (connection.reader === currentReader) {
        connection.reader = null;
      }
    }
  }

  if (!connection.closing && connection === activeConnection) {
    await closeConnection(
      connection,
      "USB disconnected",
      "The Amulet connection ended. Your edits remain in this page.",
      "error",
      true
    );
  }
}

function delay(milliseconds) {
  return new Promise((resolve) => window.setTimeout(resolve, milliseconds));
}

function resetConnectionUi(connection, label, message, kind) {
  if (connection !== activeConnection) {
    return;
  }
  activeConnection = null;
  setDeviceActionsEnabled(false);
  elements.connectButton.textContent = "Connect Amulet";
  setConnectionState("idle", label);
  setConnectionFeedback(message, kind);
}

async function closeConnection(connection, label, message, kind = "quiet", fromReader = false) {
  if (connection.closePromise) {
    return connection.closePromise;
  }

  connection.closing = true;
  clearBootstrap(connection);
  interruptOperation(connection);
  connection.closePromise = (async () => {
    if (connection.reader && !fromReader) {
      try {
        await connection.reader.cancel();
      } catch {
        // A removed USB device may reject cancellation.
      }
    }
    if (connection.readPromise && !fromReader) {
      await connection.readPromise;
    }
    await connection.writeChain.catch(() => undefined);
    try {
      await connection.port.close();
    } catch {
      // The operating system may already have removed the port.
    }
    resetConnectionUi(connection, label, message, kind);
  })();
  return connection.closePromise;
}

async function connect() {
  if (connectBusy) {
    return;
  }
  connectBusy = true;
  elements.connectButton.disabled = true;

  if (activeConnection) {
    const connection = activeConnection;
    await closeConnection(
      connection,
      "USB disconnected",
      "Disconnected. Unsaved edits remain in this page."
    );
    connectBusy = false;
    elements.connectButton.disabled = false;
    return;
  }

  let selectedPort = null;
  try {
    setConnectionState("working", "Choosing a port");
    setConnectionFeedback("Choose the USB serial port for one Amulet.", "working");
    selectedPort = await navigator.serial.requestPort();
    await selectedPort.open({ baudRate: 115200 });
    if (selectedPort.setSignals) {
      try {
        await selectedPort.setSignals({ dataTerminalReady: false, requestToSend: false });
      } catch {
        // Some serial drivers do not expose modem-control signals.
      }
    }

    const connection = {
      port: selectedPort,
      reader: null,
      readPromise: null,
      writeChain: Promise.resolve(),
      receiveBuffer: "",
      configurationsReceived: new Set(),
      preservedEditors: new Set(),
      bootstrapActive: false,
      bootstrapTarget: null,
      bootstrapTimer: null,
      pendingOperation: null,
      operationTimer: null,
      closing: false,
      closePromise: null,
    };
    activeConnection = connection;
    elements.connectButton.textContent = "Disconnect";
    setConnectionState("working", "Reading settings");
    setConnectionFeedback("The board may restart once while the serial link opens.", "working");
    connection.readPromise = readSerial(connection);
    void startBootstrap(connection, 650);
  } catch (error) {
    if (selectedPort) {
      try {
        await selectedPort.close();
      } catch {
        // Ignore cleanup errors after a failed open.
      }
    }
    if (error.name === "NotFoundError") {
      setConnectionState("idle", "Waiting for USB");
      setConnectionFeedback("No port selected. Your compositions are still here.");
    } else {
      setConnectionState("error", "Connection failed");
      setConnectionFeedback(error.message, "error");
    }
  } finally {
    connectBusy = false;
    elements.connectButton.disabled = false;
  }
}

function bindEditor(editor) {
  NOTE_NAMES.forEach((name, root) => {
    editor.elements.scaleRoot.append(new Option(name, String(root)));
  });
  Object.entries(SCALES).forEach(([type, scale]) => {
    editor.elements.scaleType.append(new Option(scale.label, type));
  });
  editor.elements.octaveUpButton.addEventListener("click", () => shiftOctave(editor, 1));
  editor.elements.octaveDownButton.addEventListener("click", () => shiftOctave(editor, -1));
  editor.elements.scaleRoot.addEventListener("change", () => {
    editor.scale.root = Number(editor.elements.scaleRoot.value);
    renderScale(editor);
  });
  editor.elements.scaleType.addEventListener("change", () => {
    editor.scale.type = editor.elements.scaleType.value;
    renderScale(editor);
    setFeedback(editor, editor.scale.type === "chromatic"
      ? "Every note is available."
      : "Sliders and arrows now stay in the scale. Notes outside it are outlined.");
  });
  editor.elements.noteDuration.addEventListener("input", () => {
    editor.noteDurationMs = Number(editor.elements.noteDuration.value);
    renderTiming(editor);
    markEditorChanged(editor);
  });
  editor.elements.noteDuration.addEventListener("change", () => {
    setFeedback(editor, `Every ${editor.label} note will last ${formatSeconds(editor.noteDurationMs)}. Preview it before saving.`);
  });
  editor.elements.startDelay.addEventListener("input", () => {
    editor.startDelayMs = Number(editor.elements.startDelay.value);
    renderTiming(editor);
    markEditorChanged(editor);
  });
  editor.elements.startDelay.addEventListener("change", () => {
    setFeedback(editor, `The ${editor.label} will wait ${formatSeconds(editor.startDelayMs)} before starting.`);
  });
  editor.elements.addStepButton.addEventListener("click", () => {
    if (editor.pattern.length >= MAX_STEPS) {
      return;
    }
    const previous = editor.pattern.at(-1);
    const lastPitch = nearestPitch(editor, editor.pattern.length);
    const target = Math.min(closestNoteIndex(lastPitch) + 2, NOTES.length - 1);
    const nextNote = NOTES[snapToScale(editor, target)];
    editor.pattern.push({ frequency: nextNote.frequency, color: previous.color });
    renderPattern(editor);
    markEditorChanged(editor, "Note added. The saved Amulet pattern is unchanged.");
    // Re-rendering moved the button, so restore focus for adding several notes.
    const nextFocus = editor.elements.addStepCell.hidden
      ? stepCard(editor, editor.pattern.length - 1).querySelector(".pitch-slider")
      : editor.elements.addStepButton;
    nextFocus.focus();
    nextFocus.scrollIntoView({ block: "nearest", inline: "nearest" });
  });
  editor.elements.paintAllButton.addEventListener("click", () => {
    editor.pattern.forEach((step) => {
      step.color = editor.elements.paintAllColor.value;
    });
    renderPattern(editor);
    markEditorChanged(editor, "Applied one color to every note. Preview it before saving.");
  });
  editor.elements.browserPreviewButton.addEventListener("click", () => {
    const wasPlaying = currentPlayback?.editor === editor;
    stopBrowserPreview();
    if (!wasPlaying) {
      playInBrowser(editor).catch((error) => {
        setFeedback(editor, `Your browser could not play audio: ${error.message}`, "error");
      });
    }
  });
  editor.elements.previewButton.addEventListener("click", () => {
    const payload = formatConfiguration(editor);
    void runOperation(
      editor,
      "preview",
      commandFor(editor, "PREVIEW", payload),
      `Sending a temporary ${editor.label} preview...`
    );
  });
  editor.elements.saveButton.addEventListener("click", () => {
    const payload = formatConfiguration(editor);
    void runOperation(
      editor,
      "save",
      commandFor(editor, "SAVE", payload),
      `Saving the ${editor.label} to this Amulet...`,
      payload
    );
  });
  editor.elements.loadButton.addEventListener("click", () => {
    void runOperation(
      editor,
      "load",
      commandFor(editor, "GET"),
      `Loading the ${editor.label} saved on this Amulet...`
    );
  });
  editor.elements.resetButton.addEventListener("click", () => {
    if (!window.confirm(editor.resetPrompt)) {
      return;
    }
    void runOperation(
      editor,
      "reset",
      commandFor(editor, "RESET"),
      `Restoring the factory ${editor.label}...`
    );
  });
}

elements.connectButton.addEventListener("click", () => void connect());
EDITOR_KEYS.forEach((key) => {
  bindEditor(editors[key]);
  renderPattern(editors[key]);
  renderScale(editors[key]);
  editors[key].elements.browserPreviewButton.hidden = !BrowserAudioContext;
});

if (!("serial" in navigator)) {
  elements.connectButton.disabled = true;
  elements.browserNote.textContent = "You can compose and play melodies in this browser. To preview on or save to an Amulet, open this page in desktop Chrome or Edge over HTTPS or localhost.";
  setConnectionState("error", "Web Serial unavailable");
  setConnectionFeedback("This browser cannot connect to an Amulet, but Play in browser still works.", "error");
} else {
  navigator.serial.addEventListener("disconnect", (event) => {
    const connection = activeConnection;
    if (connection && event.target === connection.port && !connection.closing) {
      void closeConnection(
        connection,
        "USB disconnected",
        "The Amulet was unplugged. Your edits remain in this page.",
        "error"
      );
    }
  });
}
