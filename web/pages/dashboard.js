// pages/dashboard.js — 仪表盘页
// 职责：注入模式选择、宿主选择、路径设置、安装/禁用/启用/卸载操作。
// 只处理本页逻辑，通过 Core 与后端通信，不直接碰其他页。
(function () {
  "use strict";

  const { $, $$, state, send, post, val, log, setProgress } = window.Core;

  // ---------- 本页事件注册 ----------
  // 版本列表
  window.Core.on("versions", (d) => {
    const sel = $("version");
    if (!sel) return;
    sel.innerHTML = "";
    (d.versions || []).forEach((v) => {
      const o = document.createElement("option");
      o.value = v; o.textContent = v;
      sel.appendChild(o);
    });
    log(d.versions && d.versions.length
      ? "检测到版本: " + d.versions.join(", ")
      : "未检测到版本，请检查 QQ 目录");
  });

  // 路径校验结果
  window.Core.on("validate", (d) => {
    const mark = $("mark-" + d.kind);
    const reason = $("reason-" + d.kind);
    if (mark) {
      mark.className = "vmark " + (d.ok ? (d.warn ? "warn" : "ok") : "bad");
    }
    if (reason) reason.textContent = d.reason || "";
  });

  // 自动检测 QQ 结果
  window.Core.on("qqRoot", (d) => {
    if (!d.path) { log("✗ 未自动检测到 QQ，请手动填写"); return; }
    $("qqRoot").value = d.path;
    log("自动检测到 QQ: " + d.path);
    state.pendingVersions = true;
    checkUpdate();
  });

  // QQ 更新状态
  window.Core.on("qqUpdate", (d) => {
    const bar = $("updateBar");
    if (!bar) return;
    if (d.state === "update") {
      $("updateText").textContent =
        "检测到新版本 " + d.pendingVersion +
        "（当前注入: " + (d.injectedVersion || "无") + "），需要重新注入。";
      bar.classList.remove("hidden");
      log("⚠ QQ 已更新到 " + d.pendingVersion);
    } else if (d.state === "ok") {
      bar.classList.add("hidden");
      log("注入已是最新（" + d.injectedVersion + "）");
    } else {
      bar.classList.add("hidden");
    }
  });

  // 下载完成 → 回填宿主路径
  window.Core.on("downloaded", (d) => {
    setProgress("");
    if (d.path) {
      $("hostSourceDir").value = d.path;
      log("✓ 宿主就绪: " + d.path);
      validateField("hostSourceDir");
    }
  });

  // 操作结果（安装/禁用/启用/卸载统一入口）
  window.Core.on("result", (d) => {
    window.Core.releaseBusy();
    setProgress("");
    if (d.ok) {
      window.Core.setStatus("success", "完成");
      log("✓ " + (d.message || "完成"));
      // 成功后刷新状态（版本 + 更新提示），保证 UI 不显示旧数据
      if (val("qqRoot")) {
        post("listVersions");
        checkUpdate();
      }
    } else {
      window.Core.setStatus("error", "失败");
      log("✗ " + (d.message || "失败"));
    }
  });

  // 进度
  window.Core.on("progress", (d) => {
    setProgress(d.message);
    log(d.message);
  });

  // ---------- 本页交互 ----------
  // 宿主选择
  $$(".host-card").forEach((card) => {
    card.addEventListener("click", () => {
      $$(".host-card").forEach((c) => c.classList.remove("selected"));
      card.classList.add("selected");
      state.host = card.dataset.host;
      log("已选择宿主: " + state.host);
    });
  });

  // 注入模式选择
  $$(".mode-item").forEach((item) => {
    item.addEventListener("click", () => {
      $$(".mode-item").forEach((m) => m.classList.remove("selected"));
      item.classList.add("selected");
      state.mode = item.dataset.mode;
      const name = item.querySelector("h3");
      log("已选择注入模式: " + (name ? name.textContent : state.mode));
    });
  });

  // 浏览按钮
  $$(".browse").forEach((btn) => {
    btn.addEventListener("click", () => {
      post("pickFolder", {
        kind: btn.dataset.kind,
        initial: val(btn.dataset.target),
      });
    });
  });

  // 路径防抖校验
  let vt = null;
  function validateField(kind) {
    clearTimeout(vt);
    vt = setTimeout(() => {
      if (state.busy) return;
      post("validatePath", { kind, path: val(kind) });
    }, 400);
  }
  ["hostSourceDir", "installDir", "qqRoot"].forEach((id) => {
    const el = $(id);
    if (el) el.addEventListener("input", () => validateField(id));
  });

  // 更新检查
  function checkUpdate() {
    if (state.busy) return;
    post("checkUpdate");
  }

  // 浏览结果
  window.Core.on("picked", (d) => {
    const input = $(d.kind);
    if (!input || !d.path) return;
    input.value = d.path;
    validateField(d.kind);
    if (d.kind === "qqRoot") {
      state.pendingVersions = true;
      checkUpdate();
    }
    log("已选择: " + d.path);
  });

  // ---------- 按钮 ----------
  $("btnInstall").addEventListener("click", () => send("install"));
  $("btnDisable").addEventListener("click", () => send("disable"));
  $("btnEnable").addEventListener("click", () => send("enable"));
  $("btnLocate").addEventListener("click", () => send("locateQq"));
  $("btnLaunchQq").addEventListener("click", () => send("launchQq"));
  if ($("btnReinject")) {
    $("btnReinject").addEventListener("click", () => send("install"));
  }
  if ($("linkKillQq")) {
    $("linkKillQq").addEventListener("click", (e) => {
      e.preventDefault();
      send("killQq");
    });
  }

  // 卸载二次确认（WebView2 无 confirm）
  let armed = false, armedTimer = null;
  $("btnUninstall").addEventListener("click", () => {
    if (!armed) {
      armed = true;
      $("btnUninstall").textContent = "再点一次确认卸载";
      armedTimer = setTimeout(() => {
        armed = false;
        $("btnUninstall").textContent = "卸载";
      }, 3000);
      return;
    }
    clearTimeout(armedTimer);
    armed = false;
    $("btnUninstall").textContent = "卸载";
    send("uninstall");
  });

  // ---------- 本页初始化 ----------
  if (!val("installDir")) $("installDir").value = "D:\\LiteLoaderQQNT";
})();