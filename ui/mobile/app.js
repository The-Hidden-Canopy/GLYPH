/*
 * GLYPH mobile-first UI preview.
 *
 * This file is intentionally standalone and has no network path. It exercises
 * the mobile interaction boundary around the future libglyph C ABI:
 * local hashing, explicit send/receive actions, synthetic state walkthroughs,
 * camera permission presentation, resume language, and the final hash gate.
 * It does not decode camera frames or claim a live optical transfer.
 */

(() => {
  "use strict";

  const MAX_PREVIEW_HASH_BYTES = 16 * 1024 * 1024;
  const SYNTHETIC_OBJECT_BYTES = 5 * 1024 * 1024;
  const SURFACE_COLUMNS = 12;
  const SURFACE_ROWS = 22;

  const state = {
    mode: "send",
    senderState: "IDLE",
    receiverState: "IDLE",
    evidence: "ui_preview",
    selectedFile: null,
    selectedFileHash: null,
    hashToken: 0,
    profile: "MP0",
    surfaceFrame: 0,
    simulationTimer: null,
    simulationResume: null,
    cameraStream: null,
    transfer: null,
    history: [],
    events: [],
  };

  const elements = {
    modeKicker: document.getElementById("mode-kicker"),
    modeTitle: document.getElementById("mode-title"),
    modeDescription: document.getElementById("mode-description"),
    sessionPill: document.getElementById("session-pill"),
    sessionState: document.getElementById("session-state"),
    boundaryTitle: document.getElementById("boundary-title"),
    boundaryText: document.getElementById("boundary-text"),
    evidenceBadge: document.getElementById("evidence-badge"),
    sendView: document.getElementById("send-view"),
    receiveView: document.getElementById("receive-view"),
    transfersView: document.getElementById("transfers-view"),
    settingsView: document.getElementById("settings-view"),
    surfaceEmpty: document.getElementById("surface-empty"),
    surfaceFrame: document.getElementById("surface-frame"),
    surfaceGrid: document.getElementById("surface-grid"),
    surfaceBadge: document.getElementById("surface-badge"),
    surfaceProfile: document.getElementById("surface-profile"),
    surfaceSequence: document.getElementById("surface-seq"),
    surfaceState: document.getElementById("surface-state"),
    profileLabel: document.getElementById("profile-label"),
    fileInput: document.getElementById("file-input"),
    chooseFile: document.getElementById("choose-file"),
    objectDetails: document.getElementById("object-details"),
    objectBadge: document.getElementById("object-badge"),
    objectName: document.getElementById("object-name"),
    objectSize: document.getElementById("object-size"),
    objectHash: document.getElementById("object-hash"),
    hashStatus: document.getElementById("hash-status"),
    startSend: document.getElementById("start-send"),
    runSyntheticSend: document.getElementById("run-synthetic-send"),
    pausePreview: document.getElementById("pause-preview"),
    cancelPreview: document.getElementById("cancel-preview"),
    sendVerified: document.getElementById("send-verified"),
    sendExpected: document.getElementById("send-expected"),
    sendAccepted: document.getElementById("send-accepted"),
    sendRejected: document.getElementById("send-rejected"),
    sendProgressBar: document.getElementById("send-progress-bar"),
    sendProgressLabel: document.getElementById("send-progress-label"),
    cameraPreview: document.getElementById("camera-preview"),
    cameraPlaceholder: document.getElementById("camera-placeholder"),
    scanOverlay: document.getElementById("scan-overlay"),
    scanLabel: document.getElementById("scan-label"),
    cameraBadge: document.getElementById("camera-badge"),
    receiveGuidance: document.getElementById("receive-guidance"),
    enableCamera: document.getElementById("enable-camera"),
    stopCamera: document.getElementById("stop-camera"),
    runSyntheticReceive: document.getElementById("run-synthetic-receive"),
    qualitySurface: document.getElementById("quality-surface"),
    qualityGeometry: document.getElementById("quality-geometry"),
    qualityCalibration: document.getElementById("quality-calibration"),
    qualityPayload: document.getElementById("quality-payload"),
    qualityHash: document.getElementById("quality-hash"),
    receiveAccepted: document.getElementById("receive-accepted"),
    receiveErased: document.getElementById("receive-erased"),
    receiveBlocks: document.getElementById("receive-blocks"),
    receiveResume: document.getElementById("receive-resume"),
    receiveProgressBar: document.getElementById("receive-progress-bar"),
    receiveProgressLabel: document.getElementById("receive-progress-label"),
    transferCount: document.getElementById("transfer-count"),
    transferList: document.getElementById("transfer-list"),
    profileSelect: document.getElementById("profile-select"),
    reducedMotion: document.getElementById("reduced-motion"),
    eventList: document.getElementById("event-list"),
    toast: document.getElementById("toast"),
  };

  const modeDetails = {
    send: {
      kicker: "LOCAL SESSION",
      title: "Send an object",
      description: "Hash a local file, then hold this screen toward a receiving camera.",
    },
    receive: {
      kicker: "LOCAL SESSION",
      title: "Receive an object",
      description: "Point the rear camera at a GLYPH surface. Nothing is promoted before verification.",
    },
    transfers: {
      kicker: "LOCAL JOURNAL",
      title: "Transfer history",
      description: "Inspect local sessions, durable checkpoints, and the final integrity gate.",
    },
    settings: {
      kicker: "LOCAL POLICY",
      title: "Safe operation",
      description: "Keep the optical surface conservative, visible, and account-free.",
    },
  };

  const sendSteps = [
    { state: "HASHING", progress: 8, accepted: 0, rejected: 0, frame: 1, label: "Hashing local object" },
    { state: "MANIFEST_READY", progress: 16, accepted: 0, rejected: 0, frame: 2, label: "Manifest ready · awaiting receiver" },
    { state: "BOOTSTRAP", progress: 28, accepted: 12, rejected: 0, frame: 14, label: "Bootstrap surface repeating" },
    { state: "TRANSMITTING", progress: 67, accepted: 144, rejected: 9, frame: 318, label: "Synthetic payload surface active" },
    { state: "FINAL_REPEAT", progress: 100, accepted: 221, rejected: 14, frame: 487, label: "Final repeat displayed · not verified" },
    { state: "DONE", progress: 100, accepted: 221, rejected: 14, frame: 490, label: "Display complete · receiver verification still required" },
  ];

  const receiveSteps = [
    { state: "SEARCHING", progress: 0, surface: "NO", geometry: "—", calibration: "—", payload: "—", guidance: "Point the rear camera at the sending screen.", label: "Searching for anchors" },
    { state: "SURFACE_FOUND", progress: 4, surface: "YES", geometry: "MEASURING", calibration: "—", payload: "—", guidance: "Surface found · hold steady.", label: "Anchors detected" },
    { state: "CALIBRATING", progress: 9, surface: "YES", geometry: "PASS", calibration: "MEASURING", payload: "BLOCKED", guidance: "Calibrating black/R/G/B/white references.", label: "Calibration in progress" },
    { state: "MANIFEST", progress: 18, surface: "YES", geometry: "PASS", calibration: "PASS", payload: "READY", guidance: "Manifest received · preparing local journal.", label: "Manifest accepted" },
    { state: "RECEIVING", progress: 58, surface: "YES", geometry: "PASS", calibration: "PASS", payload: "ACCEPTING", guidance: "Keep the sending surface inside the frame.", label: "Payload reception" },
    { state: "RECOVERING", progress: 82, surface: "YES", geometry: "PASS", calibration: "PASS", payload: "FEC", guidance: "Recovering missing blocks · hold steady.", label: "FEC recovery" },
    { state: "VERIFYING", progress: 100, surface: "YES", geometry: "PASS", calibration: "PASS", payload: "DONE", guidance: "Verifying exact length and SHA-256.", label: "Integrity verification" },
    { state: "INTEGRITY_GATE_BLOCKED", progress: 100, surface: "YES", geometry: "PASS", calibration: "PASS", payload: "DONE", guidance: "Synthetic path stopped: source and destination hashes are not available.", label: "COMPLETE blocked · hash equality required" },
  ];

  function formatBytes(value) {
    if (!Number.isFinite(value)) return "—";
    if (value === 0) return "0 B";
    const units = ["B", "KiB", "MiB", "GiB"];
    const index = Math.min(Math.floor(Math.log(value) / Math.log(1024)), units.length - 1);
    return `${(value / (1024 ** index)).toFixed(index === 0 ? 0 : 2)} ${units[index]}`;
  }

  function nowLabel() {
    return new Date().toLocaleTimeString([], { hour: "2-digit", minute: "2-digit", second: "2-digit" });
  }

  function showToast(message) {
    elements.toast.textContent = message;
    elements.toast.hidden = false;
    clearTimeout(showToast.timer);
    showToast.timer = setTimeout(() => {
      elements.toast.hidden = true;
    }, 4200);
  }

  function logEvent(message) {
    state.events.unshift({ time: nowLabel(), message });
    state.events = state.events.slice(0, 8);
    elements.eventList.replaceChildren();
    state.events.forEach((event) => {
      const item = document.createElement("li");
      const time = document.createElement("time");
      const text = document.createElement("span");
      time.textContent = event.time;
      text.textContent = event.message;
      item.append(time, text);
      elements.eventList.appendChild(item);
    });
  }

  function setEvidence(label, detail) {
    elements.evidenceBadge.textContent = label;
    elements.boundaryTitle.textContent = label === "SYNTHETIC" ? "Synthetic preview boundary" : "Local preview boundary";
    elements.boundaryText.textContent = detail;
    elements.evidenceBadge.className = `status-chip ${label === "SYNTHETIC" ? "status-chip--synthetic" : "status-chip--quiet"}`;
    state.evidence = label.toLowerCase();
  }

  function setSessionState(label, kind = "quiet") {
    elements.sessionState.textContent = label;
    elements.sessionPill.classList.toggle("is-active", kind === "active");
    elements.sessionPill.classList.toggle("is-failed", kind === "failed");
  }

  function setMode(mode) {
    state.mode = mode;
    const details = modeDetails[mode];
    elements.modeKicker.textContent = details.kicker;
    elements.modeTitle.textContent = details.title;
    elements.modeDescription.textContent = details.description;
    elements.sendView.hidden = mode !== "send";
    elements.receiveView.hidden = mode !== "receive";
    elements.transfersView.hidden = mode !== "transfers";
    elements.settingsView.hidden = mode !== "settings";
    document.querySelectorAll(".nav-button").forEach((button) => {
      button.classList.toggle("is-active", button.dataset.mode === mode);
    });
    if (mode === "send") {
      setSessionState(state.senderState, state.senderState === "IDLE" ? "quiet" : "active");
    } else if (mode === "receive") {
      setSessionState(state.receiverState, state.receiverState === "IDLE" ? "quiet" : "active");
    } else {
      setSessionState(mode === "transfers" ? "JOURNAL" : "SETTINGS");
    }
    if (mode === "transfers") renderTransfers();
    logEvent(`Opened ${mode} surface.`);
  }

  function updateProfileLabel() {
    const isStandard = state.profile === "MP1";
    elements.profileLabel.textContent = isStandard ? "MP1 · Standard Mobile" : "MP0 · Robust Mobile Bootstrap";
    elements.surfaceProfile.textContent = state.profile;
  }

  function updateObjectDetails() {
    const file = state.selectedFile;
    elements.objectDetails.hidden = !file;
    elements.startSend.disabled = !(file && state.selectedFileHash);
    if (!file) {
      elements.objectBadge.textContent = "NOT SELECTED";
      elements.objectBadge.className = "micro-badge";
      return;
    }
    elements.objectBadge.textContent = state.selectedFileHash ? "HASHED" : "HASH PENDING";
    elements.objectBadge.className = `micro-badge ${state.selectedFileHash ? "is-good" : "is-warn"}`;
    elements.objectName.textContent = file.name;
    elements.objectSize.textContent = formatBytes(file.size);
    elements.objectHash.textContent = state.selectedFileHash || "—";
  }

  function bytesToHex(bytes) {
    return Array.from(new Uint8Array(bytes), (byte) => byte.toString(16).padStart(2, "0")).join("");
  }

  async function hashSelectedFile(file, token) {
    if (!globalThis.crypto?.subtle) {
      elements.hashStatus.textContent = "Hashing unavailable in this browser context.";
      elements.hashStatus.style.color = "var(--red)";
      return;
    }
    if (file.size > MAX_PREVIEW_HASH_BYTES) {
      elements.hashStatus.textContent = "Preview hash limit reached; use the native libglyph backend for this object.";
      elements.hashStatus.style.color = "var(--amber)";
      return;
    }
    try {
      elements.hashStatus.textContent = "Hashing locally…";
      elements.hashStatus.style.color = "var(--cyan)";
      const digest = await globalThis.crypto.subtle.digest("SHA-256", await file.arrayBuffer());
      if (token !== state.hashToken || file !== state.selectedFile) return;
      state.selectedFileHash = bytesToHex(digest);
      elements.hashStatus.textContent = "SHA-256 computed locally.";
      elements.hashStatus.style.color = "var(--teal)";
      updateObjectDetails();
      setEvidence("LOCAL HASH", "The selected file was hashed locally. No transfer backend is attached to this preview.");
      logEvent(`SHA-256 computed locally for ${file.name}.`);
    } catch (error) {
      if (token !== state.hashToken) return;
      state.selectedFileHash = null;
      elements.hashStatus.textContent = "Hash failed locally; no transfer can start.";
      elements.hashStatus.style.color = "var(--red)";
      updateObjectDetails();
      logEvent("Local hashing failed; transfer remains blocked.");
    }
  }

  function selectFile() {
    elements.fileInput.click();
  }

  function onFileSelected(event) {
    const [file] = event.target.files || [];
    state.hashToken += 1;
    state.selectedFile = file || null;
    state.selectedFileHash = null;
    if (!file) {
      elements.hashStatus.textContent = "Hash not computed.";
      updateObjectDetails();
      return;
    }
    elements.hashStatus.textContent = "Preparing local hash…";
    elements.hashStatus.style.color = "var(--cyan)";
    updateObjectDetails();
    logEvent(`Selected local object ${file.name} (${formatBytes(file.size)}).`);
    hashSelectedFile(file, state.hashToken);
  }

  function clearSimulation() {
    if (state.simulationTimer !== null) {
      clearTimeout(state.simulationTimer);
      state.simulationTimer = null;
    }
    state.simulationResume = null;
    elements.pausePreview.disabled = true;
    elements.cancelPreview.disabled = true;
  }

  function renderSurface(frame) {
    elements.surfaceGrid.replaceChildren();
    const classes = ["", "--r", "--g", "--b", "--rg", "--rb", "--gb", "--rgb"];
    for (let row = 0; row < SURFACE_ROWS; row += 1) {
      for (let column = 0; column < SURFACE_COLUMNS; column += 1) {
        const cell = document.createElement("span");
        const symbol = (row * 17 + column * 31 + frame * 7 + ((row ^ column) * 3)) % 8;
        cell.className = `surface-cell${classes[symbol]}`;
        elements.surfaceGrid.appendChild(cell);
      }
    }
    elements.surfaceSequence.textContent = `FRAME ${String(frame).padStart(4, "0")}`;
  }

  function renderSendMetrics(step) {
    const transfer = state.transfer;
    if (!transfer) {
      elements.sendVerified.textContent = "—";
      elements.sendExpected.textContent = "—";
      elements.sendAccepted.textContent = "—";
      elements.sendRejected.textContent = "—";
      elements.sendProgressBar.style.width = "0%";
      elements.sendProgressLabel.textContent = "No transfer session.";
      return;
    }
    elements.sendVerified.textContent = "NOT VERIFIED";
    elements.sendExpected.textContent = `${formatBytes(transfer.expectedBytes)} · synthetic`;
    elements.sendAccepted.textContent = String(step.accepted);
    elements.sendRejected.textContent = String(step.rejected);
    elements.sendProgressBar.style.width = `${step.progress}%`;
    elements.sendProgressLabel.textContent = `${step.progress}% display progress · ${step.label}`;
  }

  function applySendStep(step) {
    state.senderState = step.state;
    state.surfaceFrame = step.frame;
    elements.surfaceEmpty.hidden = true;
    elements.surfaceFrame.hidden = false;
    elements.surfaceBadge.textContent = step.state === "DONE" ? "DISPLAY COMPLETE" : "SYNTHETIC ACTIVE";
    elements.surfaceBadge.className = `micro-badge ${step.state === "DONE" ? "is-warn" : "is-good"}`;
    elements.surfaceState.textContent = step.state === "DONE" ? "DISPLAY COMPLETE · NOT VERIFIED" : "SYNTHETIC SURFACE PREVIEW";
    renderSurface(step.frame);
    renderSendMetrics(step);
    setSessionState(step.state === "DONE" ? "DISPLAY COMPLETE" : step.state, step.state === "DONE" ? "quiet" : "active");
    logEvent(step.label + ".");
  }

  function runSyntheticSequence(steps, applyStep, finish) {
    clearSimulation();
    let index = 0;
    const advance = () => {
      if (index >= steps.length) {
        state.simulationTimer = null;
        state.simulationResume = null;
        finish();
        return;
      }
      applyStep(steps[index]);
      index += 1;
      state.simulationTimer = setTimeout(advance, 850);
    };
    state.simulationResume = () => {
      if (state.simulationTimer === null && index < steps.length) {
        state.simulationTimer = setTimeout(advance, 0);
      }
    };
    advance();
  }

  function startSyntheticSend() {
    state.mode = "send";
    setMode("send");
    state.transfer = {
      id: `synthetic-${Date.now()}`,
      name: state.selectedFile?.name || "Synthetic preview object",
      expectedBytes: state.selectedFile?.size || SYNTHETIC_OBJECT_BYTES,
      sourceHash: state.selectedFileHash || null,
      verified: false,
      status: "DISPLAY COMPLETE · NOT VERIFIED",
      evidence: "synthetic simulation",
    };
    setEvidence("SYNTHETIC", "Synthetic walkthrough only. It demonstrates UI state transitions and cannot prove camera capture, reconstruction, or SHA-256 equality.");
    runSyntheticSequence(sendSteps, applySendStep, () => {
      elements.pausePreview.disabled = true;
      elements.cancelPreview.disabled = false;
      state.history.unshift(state.transfer);
      state.history = state.history.slice(0, 8);
      renderTransfers();
      showToast("Display preview finished. COMPLETE remains blocked until a receiver verifies the object.");
    });
    elements.pausePreview.disabled = false;
    elements.cancelPreview.disabled = false;
  }

  function startNativeSend() {
    if (!state.selectedFile || !state.selectedFileHash) {
      showToast("Select a file and compute its local SHA-256 before sending.");
      return;
    }
    setEvidence("LOCAL HASH", "The object is hashed locally, but no native sender backend is connected to this UI preview.");
    setSessionState("BACKEND UNAVAILABLE", "failed");
    elements.surfaceBadge.textContent = "BACKEND UNAVAILABLE";
    elements.surfaceBadge.className = "micro-badge is-bad";
    logEvent("Native sender request blocked: libglyph sender backend is not connected.");
    showToast("Native sender unavailable in this preview. No transmission started.");
  }

  function pausePreview() {
    if (state.simulationTimer !== null) {
      clearTimeout(state.simulationTimer);
      state.simulationTimer = null;
      elements.pausePreview.textContent = "Resume preview";
      setSessionState("PAUSED", "quiet");
      logEvent("Synthetic sender preview paused by user.");
      return;
    }
    if (state.simulationResume) {
      state.simulationResume();
      elements.pausePreview.textContent = "Pause";
      setSessionState(state.senderState, "active");
      logEvent("Synthetic sender preview resumed by user.");
    }
  }

  function cancelPreview() {
    clearSimulation();
    state.senderState = "CANCELLED";
    elements.surfaceState.textContent = "CANCELLED · NOT VERIFIED";
    elements.surfaceBadge.textContent = "CANCELLED";
    elements.surfaceBadge.className = "micro-badge is-warn";
    setSessionState("CANCELLED", "failed");
    logEvent("Sender preview cancelled; no object was promoted.");
  }

  async function enableCamera() {
    if (!navigator.mediaDevices?.getUserMedia) {
      setSessionState("CAMERA UNAVAILABLE", "failed");
      elements.cameraBadge.textContent = "UNAVAILABLE";
      elements.cameraBadge.className = "micro-badge is-bad";
      logEvent("Camera backend unavailable in this browser context.");
      showToast("Camera access is unavailable here. The decoder remains unattached.");
      return;
    }
    try {
      state.cameraStream = await navigator.mediaDevices.getUserMedia({
        video: { facingMode: { ideal: "environment" } },
        audio: false,
      });
      elements.cameraPreview.srcObject = state.cameraStream;
      elements.cameraPreview.hidden = false;
      elements.cameraPlaceholder.hidden = true;
      elements.scanOverlay.hidden = false;
      elements.scanLabel.textContent = "CAMERA PREVIEW · DECODER UNAVAILABLE";
      elements.cameraBadge.textContent = "CAMERA ACTIVE";
      elements.cameraBadge.className = "micro-badge is-good";
      elements.enableCamera.disabled = true;
      elements.stopCamera.disabled = false;
      setEvidence("CAMERA PREVIEW", "Camera frames are shown locally. No frame is uploaded, decoded, or treated as transfer evidence by this preview.");
      setSessionState("SEARCHING", "active");
      logEvent("Camera preview enabled locally; decoder is not attached.");
    } catch (error) {
      setSessionState("CAMERA DENIED", "failed");
      elements.cameraBadge.textContent = "DENIED";
      elements.cameraBadge.className = "micro-badge is-bad";
      logEvent("Camera permission was not granted; receive remains unavailable.");
      showToast("Camera permission was not granted. No receive session started.");
    }
  }

  function stopCamera() {
    if (state.cameraStream) {
      state.cameraStream.getTracks().forEach((track) => track.stop());
      state.cameraStream = null;
    }
    elements.cameraPreview.srcObject = null;
    elements.cameraPreview.hidden = true;
    elements.cameraPlaceholder.hidden = false;
    elements.scanOverlay.hidden = true;
    elements.cameraBadge.textContent = "NOT CONNECTED";
    elements.cameraBadge.className = "micro-badge";
    elements.enableCamera.disabled = false;
    elements.stopCamera.disabled = true;
    setSessionState("IDLE");
    logEvent("Camera preview stopped.");
  }

  function setQuality(element, value, kind = "") {
    element.textContent = value;
    element.className = kind ? `is-${kind}` : "";
  }

  function applyReceiveStep(step) {
    state.receiverState = step.state;
    setQuality(elements.qualitySurface, step.surface, step.surface === "YES" ? "good" : "");
    setQuality(elements.qualityGeometry, step.geometry, step.geometry === "PASS" ? "good" : "");
    setQuality(elements.qualityCalibration, step.calibration, step.calibration === "PASS" ? "good" : "");
    setQuality(elements.qualityPayload, step.payload, step.payload === "DONE" ? "good" : "");
    elements.receiveGuidance.textContent = step.guidance;
    elements.receiveAccepted.textContent = step.progress > 0 ? String(Math.round(step.progress * 2.1)) : "—";
    elements.receiveErased.textContent = step.progress >= 58 ? String(Math.max(0, Math.round((100 - step.progress) / 3))) : "—";
    elements.receiveBlocks.textContent = step.progress >= 18 ? String(Math.max(0, Math.round(step.progress / 8))) : "—";
    elements.receiveResume.textContent = step.progress >= 58 ? "CHECKPOINTED" : "NONE";
    elements.receiveProgressBar.style.width = `${step.progress}%`;
    elements.receiveProgressLabel.textContent = `${step.progress}% synthetic path · ${step.label}`;
    setSessionState(step.state === "INTEGRITY_GATE_BLOCKED" ? "COMPLETE BLOCKED" : step.state, step.state === "INTEGRITY_GATE_BLOCKED" ? "failed" : "active");
    logEvent(step.label + ".");
  }

  function startSyntheticReceive() {
    setMode("receive");
    setEvidence("SYNTHETIC", "Synthetic receiver walkthrough only. The final integrity gate will remain blocked because no source/destination hash pair exists.");
    elements.qualityHash.textContent = "REQUIRED";
    elements.qualityHash.className = "is-bad";
    runSyntheticSequence(receiveSteps, applyReceiveStep, () => {
      const transfer = {
        id: `synthetic-receive-${Date.now()}`,
        name: "Synthetic receiver walkthrough",
        expectedBytes: null,
        sourceHash: null,
        verified: false,
        status: "COMPLETE BLOCKED · HASH REQUIRED",
        evidence: "synthetic simulation",
      };
      state.history.unshift(transfer);
      state.history = state.history.slice(0, 8);
      renderTransfers();
      showToast("Integrity gate held: no object was promoted.");
    });
  }

  function renderTransfers() {
    elements.transferCount.textContent = `${state.history.length} SESSION${state.history.length === 1 ? "" : "S"}`;
    elements.transferList.replaceChildren();
    if (state.history.length === 0) {
      const empty = document.createElement("div");
      empty.className = "empty-state";
      const mark = document.createElement("span");
      const title = document.createElement("strong");
      const copy = document.createElement("p");
      mark.textContent = "⌁";
      title.textContent = "No local transfer sessions";
      copy.textContent = "Interrupted or synthetic sessions will appear here. Unverified bytes are never presented as complete.";
      empty.append(mark, title, copy);
      elements.transferList.appendChild(empty);
      return;
    }
    state.history.forEach((transfer) => {
      const card = document.createElement("article");
      card.className = "transfer-card";
      const header = document.createElement("div");
      header.className = "transfer-card-header";
      const name = document.createElement("strong");
      const badge = document.createElement("span");
      const id = document.createElement("small");
      const details = document.createElement("p");
      name.textContent = transfer.name;
      badge.className = `micro-badge ${transfer.verified ? "is-good" : "is-warn"}`;
      badge.textContent = transfer.verified ? "VERIFIED" : "NOT VERIFIED";
      id.textContent = `transfer_id=${transfer.id}`;
      details.textContent = `${transfer.status} · evidence=${transfer.evidence}`;
      header.append(name, badge);
      card.append(header, id, details);
      elements.transferList.appendChild(card);
    });
  }

  function setReducedMotion() {
    document.body.classList.toggle("reduce-motion", elements.reducedMotion.checked);
  }

  document.querySelectorAll(".nav-button").forEach((button) => {
    button.addEventListener("click", () => setMode(button.dataset.mode));
  });
  elements.chooseFile.addEventListener("click", selectFile);
  elements.fileInput.addEventListener("change", onFileSelected);
  elements.startSend.addEventListener("click", startNativeSend);
  elements.runSyntheticSend.addEventListener("click", startSyntheticSend);
  elements.pausePreview.addEventListener("click", pausePreview);
  elements.cancelPreview.addEventListener("click", cancelPreview);
  elements.enableCamera.addEventListener("click", enableCamera);
  elements.stopCamera.addEventListener("click", stopCamera);
  elements.runSyntheticReceive.addEventListener("click", startSyntheticReceive);
  elements.profileSelect.addEventListener("change", () => {
    state.profile = elements.profileSelect.value;
    updateProfileLabel();
    logEvent(`Selected ${state.profile} profile.`);
  });
  elements.reducedMotion.addEventListener("change", setReducedMotion);
  window.addEventListener("beforeunload", stopCamera);

  updateProfileLabel();
  renderTransfers();
  logEvent("Waiting for an explicit local action.");
})();
