// pages/plugins.js — 插件页
// 职责：扫描并展示 installDir/plugins/*/manifest.json 的已装插件。
(function () {
  "use strict";

  const { $, val, post, log, setProgress } = window.Core;

  function renderPlugins(items) {
    const list = $("pluginList");
    if (!list) return;
    if (!items.length) {
      list.textContent = "未找到插件（宿主安装后自动扫描）";
      return;
    }
    list.innerHTML = "";
    items.forEach((it) => {
      const row = document.createElement("div");
      row.className = "host-row";
      row.innerHTML =
        '<div class="host-icon">◈</div>' +
        '<div style="flex:1"><div class="name">' + (it.name || "?") +
        (it.version ? ' <span class="meta">v' + it.version + "</span>" : "") + "</div>" +
        (it.description ? '<div class="path">' + it.description + "</div>" : "") + "</div>" +
        (it.author ? '<div class="meta">by ' + it.author + "</div>" : "");
      list.appendChild(row);
    });
  }

  window.Core.on("plugins", (d) => renderPlugins(d.items || []));

  window.Core.on("result", (d) => {
    window.Core.releaseBusy();
    setProgress("");
    log((d.ok ? "✓ " : "✗ ") + (d.message || ""));
  });

  // 进入本页时刷新
  window.Core.onPluginsRefresh = () => post("listPlugins");
})();