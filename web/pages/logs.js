// pages/logs.js — 日志页
// 职责：展示/清空运行日志、打开日志目录。
(function () {
  "use strict";

  const { $, send, log } = window.Core;

  if ($("btnClearLog")) {
    $("btnClearLog").addEventListener("click", () => {
      const el = $("log");
      if (el) el.textContent = "";
    });
  }
  if ($("btnOpenLog")) {
    $("btnOpenLog").addEventListener("click", () => send("openFolder", { path: "__data__" }));
  }

  log("日志页已加载");
})();