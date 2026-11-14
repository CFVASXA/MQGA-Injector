// app.js — 入口：初始化、消息总线、页面注册
// 只做「装配」：接收后端消息 → 分发给页面；页面刷新钩子挂到导航。
// 所有业务逻辑在 pages/ 下，各页只处理自己的部分。
(function () {
  "use strict";

  const { $, $$, state, post, log, setStatus, dispatch, initNav } = window.Core;

  // ---------- 后端消息总线 ----------
  // WebView2 postWebMessageAsJson → e.data 已是对象；兼容字符串情况。
  window.chrome.webview.addEventListener("message", (e) => {
    const data = typeof e.data === "string" ? JSON.parse(e.data) : e.data;
    const ev = data && data.event;
    if (!ev) return;

    // busy 状态：驱动按钮禁用视觉 + 排队请求
    if (ev === "busy") {
      state.busy = data.busy;
      document.body.classList.toggle("is-busy", state.busy);
      if (!state.busy && state.pendingVersions) {
        state.pendingVersions = false;
        post("listVersions");
      }
      return;
    }

    // init：回填已存配置
    if (ev === "init") {
      const c = data.config || {};
      if (c.hostSourceDir) $("hostSourceDir").value = c.hostSourceDir;
      if (c.installDir) $("installDir").value = c.installDir;
      if (c.qqRoot) $("qqRoot").value = c.qqRoot;
      if (c.version) $("version").value = c.version;
      state.host = c.host || state.host;
      state.mode = c.mode || state.mode;
      syncModeUI();
      log("已加载保存的配置（模式: " + state.mode + "）");
      return;
    }

    dispatch(ev, data);
  });

  // 模式选择 UI 与 state 同步（配置加载后校正选中态）
  function syncModeUI() {
    $$(".mode-item").forEach((m) => {
      m.classList.toggle("selected", m.dataset.mode === state.mode);
    });
    $$(".host-card").forEach((c) => {
      c.classList.toggle("selected", c.dataset.host === state.host);
    });
  }

  // ---------- 导航 + 页面刷新钩子 ----------
  initNav((page) => {
    const refresh = {
      host: () => window.Core.onHostsRefresh && window.Core.onHostsRefresh(),
      plugins: () => window.Core.onPluginsRefresh && window.Core.onPluginsRefresh(),
      tools: () => window.Core.onToolsRefresh && window.Core.onToolsRefresh(),
    }[page];
    if (refresh) refresh();
  });

  // ---------- 启动 ----------
  if (!state.busy) setStatus("", "空闲");
  log("注入器已就绪");
  // 通知后端：JS 监听器已注册，可回发配置
  post("ready");
})();