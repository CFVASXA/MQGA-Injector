// pages/tools.js — 工具页
// 职责：生成 preload.js、检测 MinHook/Manual Map 工具 DLL 就绪状态。
(function () {
  "use strict";

  const { $, send, post, log, setProgress } = window.Core;

  // 工具就绪状态渲染
  function renderStatus(kind, ready, path) {
    const el = $(kind === "manualmap" ? "manualmapStatus" : "minhookStatus");
    if (!el) return;
    el.innerHTML =
      '<div class="host-row"><div class="host-icon' + (ready ? "" : " alt") + '">' +
      (ready ? "✓" : "✗") + "</div>" +
      '<div style="flex:1"><div class="name">' + (ready ? "已就绪" : "未就绪") + "</div>" +
      '<div class="path">' + (path || "") + "</div></div>" +
      (ready ? '<span class="tag ok">可用</span>' : '<span class="tag">缺失</span>') + "</div>";
  }

  window.Core.on("toolStatus", (d) => renderStatus(d.kind, d.ready, d.path));

  window.Core.on("result", (d) => {
    window.Core.releaseBusy();
    setProgress("");
    log((d.ok ? "✓ " : "✗ ") + (d.message || ""));
  });

  window.Core.on("progress", (d) => {
    setProgress(d.message);
    log(d.message);
  });

  // 按钮
  if ($("btnGenPreload")) $("btnGenPreload").addEventListener("click", () => send("genPreload"));
  if ($("btnCheckMinhook")) $("btnCheckMinhook").addEventListener("click", () => post("checkMinhook"));
  if ($("btnCheckProxy")) $("btnCheckProxy").addEventListener("click", () => post("checkProxy"));
  if ($("btnCheckManualmap")) $("btnCheckManualmap").addEventListener("click", () => post("checkManualmap"));
  if ($("btnOpenData")) $("btnOpenData").addEventListener("click", () => send("openFolder", { path: "__data__" }));

  // 进入本页时检测
  window.Core.onToolsRefresh = () => {
    post("checkMinhook");
    post("checkManualmap");
  };
})();