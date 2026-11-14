// pages/settings.js — 设置页
// 职责：高级选项（自动重注入、Manual Map 工具路径等）。
(function () {
  "use strict";

  const { $, $$, log } = window.Core;

  // 目前设置保存在内存（刷新后重置）；后续可扩展为写 config.json。
  $$("#page-settings input, #page-settings select").forEach((el) => {
    el.addEventListener("change", () => {
      const label = el.previousElementSibling;
      log("设置已更新: " + (label ? label.textContent.trim() : el.id));
    });
  });

  if ($("minhookDllPath")) {
    log("设置页已加载");
  }
})();