const $ = id => document.getElementById(id);
const $$ = sel => document.querySelectorAll(sel);

let busy = false;
let selectedHost = 'mqga';
let pendingVersions = false;

/* ---------- 页面切换 ---------- */
$$('.nav-item').forEach(el => {
    el.addEventListener('click', () => {
        $$('.nav-item').forEach(n => n.classList.remove('active'));
        $$('.page').forEach(p => p.classList.remove('active'));
        el.classList.add('active');
        $('page-' + el.dataset.page).classList.add('active');
        if (el.dataset.page === 'host' && !busy) {
            window.chrome.webview.postMessage({ action: 'listHosts' });
        }
        if (el.dataset.page === 'plugins' && !busy) {
            window.chrome.webview.postMessage({
                action: 'listPlugins',
                installDir: $('installDir').value.trim(),
                host: selectedHost,
            });
        }
    });
});

/* ---------- 宿主卡片 ---------- */
$$('.host-card').forEach(card => {
    card.addEventListener('click', () => {
        $$('.host-card').forEach(c => c.classList.remove('selected'));
        card.classList.add('selected');
        selectedHost = card.dataset.host;
        log('已选择宿主: ' + selectedHost);
    });
});

/* ---------- 工具 ---------- */
function log(msg) {
    const el = $('log');
    const time = new Date().toLocaleTimeString('zh-CN');
    el.textContent += `[${time}] ${msg}\n`;
    el.scrollTop = el.scrollHeight;
}

function setStatus(state, text) {
    $('ring').className = 'status-ring ' + state;
    $('ringDot').className = 'ring-dot ' + state;
    $('statusText').textContent = text;
}

function setProgress(text) {
    $('progressText').textContent = text;
}

function send(action, extra = {}) {
    if (busy) return;
    busy = true;
    setStatus('running', '执行中…');
    window.chrome.webview.postMessage({
        action,
        host: selectedHost,
        hostSourceDir: $('hostSourceDir').value.trim(),
        installDir: $('installDir').value.trim(),
        qqRoot: $('qqRoot').value.trim(),
        version: $('version').value.trim(),
        ...extra,
    });
}

/* ---------- 消息处理 ---------- */
window.chrome.webview.addEventListener('message', e => {
    const data = typeof e.data === 'string' ? JSON.parse(e.data) : e.data;
    const ev = data.event;

    try { window.chrome.webview.postMessage({ action: 'jsAck', event: ev }); } catch (_) {}

    if (ev === 'progress') {
        setProgress(data.message);
        log(data.message);
        return;
    }
    if (ev === 'busy') {
        busy = data.busy;
        document.body.classList.toggle('is-busy', busy);
        if (!busy && pendingVersions) {
            pendingVersions = false;
            send('listVersions');
        }
        return;
    }
    if (ev === 'init') {
        const c = data.config || {};
        if (c.hostSourceDir) $('hostSourceDir').value = c.hostSourceDir;
        if (c.installDir) $('installDir').value = c.installDir;
        if (c.qqRoot) $('qqRoot').value = c.qqRoot;
        if (c.version) $('version').value = c.version;
        log('已加载保存的配置');
        return;
    }
    if (ev === 'versions') {
        const sel = $('version');
        sel.innerHTML = '';
        (data.versions || []).forEach(v => {
            const opt = document.createElement('option');
            opt.value = v; opt.textContent = v;
            sel.appendChild(opt);
        });
        log(data.versions && data.versions.length
            ? '检测到版本: ' + data.versions.join(', ')
            : '未检测到版本，请检查 QQ 目录');
        return;
    }
    if (ev === 'qqRoot') {
        if (data.path) {
            $('qqRoot').value = data.path;
            log('自动检测到 QQ 目录: ' + data.path);
            pendingVersions = true;
            checkUpdate();
        } else {
            log('✗ 未自动检测到 QQ，请手动填写');
        }
        return;
    }
    if (ev === 'validate') {
        const mark = $('mark-' + data.kind);
        const reason = $('reason-' + data.kind);
        if (mark) mark.className = 'vmark ' + (data.ok ? (data.warn ? 'warn' : 'ok') : 'bad');
        if (reason) reason.textContent = data.reason || '';
        return;
    }
    if (ev === 'picked') {
        if (data.path) {
            const input = $(data.kind);
            if (input) {
                input.value = data.path;
                validateField(data.kind);
                if (data.kind === 'qqRoot') {
                    pendingVersions = true;
                    checkUpdate();
                }
                log('已选择: ' + data.path);
            }
        }
        return;
    }
    if (ev === 'downloaded') {
        setProgress('');
        setStatus('success', '完成');
        log('✓ 宿主就绪: ' + data.path);
        $('hostSourceDir').value = data.path;
        return;
    }
    if (ev === 'hosts') {
        const list = $('hostList');
        const items = data.items || [];
        if (!items.length) { list.textContent = '还没有下载宿主（点上方按钮下载）'; return; }
        list.innerHTML = '';
        items.forEach(it => {
            const row = document.createElement('div');
            row.className = 'host-row';
            row.innerHTML = '<div class="host-icon' + (it.ready ? '' : ' alt') + '">' +
                (it.name.indexOf('liteloader') >= 0 ? 'LL' : 'MQ') + '</div>' +
                '<div style="flex:1"><div class="name">' + it.name + '</div>' +
                '<div class="path">' + (it.ready ? '就绪（含 src/main.js）' : '不完整（缺 src/main.js）') + '</div></div>' +
                (it.ready ? '<span class="tag ok">可用</span>' : '<span class="tag">不完整</span>');
            list.appendChild(row);
        });
        return;
    }
    if (ev === 'plugins') {
        const list = $('pluginList');
        const items = data.items || [];
        if (!items.length) { list.textContent = '未找到插件（宿主安装后自动扫描）'; return; }
        list.innerHTML = '';
        items.forEach(it => {
            const row = document.createElement('div');
            row.className = 'host-row';
            row.innerHTML = '<div><div class="name">' + (it.name || '?') +
                ' <span class="meta">' + (it.version ? 'v' + it.version : '') + '</span></div>' +
                (it.description ? '<div class="path">' + it.description + '</div>' : '') + '</div>' +
                (it.author ? '<div class="meta">by ' + it.author + '</div>' : '');
            list.appendChild(row);
        });
        return;
    }
    if (ev === 'qqUpdate') {
        const bar = $('updateBar');
        if (data.state === 'update') {
            $('updateText').textContent = '检测到新版本 ' + data.pendingVersion +
                '（当前注入: ' + (data.injectedVersion || '无') + '），需要重新注入。';
            bar.classList.remove('hidden');
            log('⚠ QQ 已更新到 ' + data.pendingVersion + '，需要重新注入');
        } else if (data.state === 'ok') {
            bar.classList.add('hidden');
            log('注入已是最新（' + data.injectedVersion + '）');
        } else {
            bar.classList.add('hidden');
        }
        return;
    }
    if (ev === 'result') {
        busy = false;
        document.body.classList.toggle('is-busy', false);
        setProgress('');
        if (data.ok) {
            setStatus('success', '完成');
            log('✓ ' + (data.message || '完成'));
            // 成功后自动刷新状态（版本列表 + 更新检查），UI 不再显示旧状态
            if ($('qqRoot').value.trim()) { send('listVersions'); checkUpdate(); }
        } else {
            setStatus('error', '失败');
            log('✗ ' + (data.message || '失败'));
        }
        return;
    }
});

/* ---------- 浏览按钮 ---------- */
$$('.browse').forEach(btn => {
    btn.addEventListener('click', () => {
        window.chrome.webview.postMessage({
            action: 'pickFolder',
            kind: btn.dataset.kind,
            initial: $(btn.dataset.target).value.trim(),
            host: selectedHost,
        });
    });
});

/* ---------- 输入框防抖校验 ---------- */
let validateTimer = null;
function validateField(kind) {
    clearTimeout(validateTimer);
    validateTimer = setTimeout(() => {
        if (busy) return;
        window.chrome.webview.postMessage({
            action: 'validatePath', kind, path: $(kind).value.trim(),
            host: selectedHost,
            hostSourceDir: $('hostSourceDir').value.trim(),
            installDir: $('installDir').value.trim(),
            qqRoot: $('qqRoot').value.trim(),
            version: $('version').value.trim(),
        });
    }, 400);
}
['hostSourceDir', 'installDir', 'qqRoot'].forEach(id => {
    $(id).addEventListener('input', () => validateField(id));
});

/* ---------- QQ 更新检查 ---------- */
function checkUpdate() {
    if (busy) return;
    window.chrome.webview.postMessage({
        action: 'checkUpdate', host: selectedHost,
        hostSourceDir: $('hostSourceDir').value.trim(),
        installDir: $('installDir').value.trim(),
        qqRoot: $('qqRoot').value.trim(),
        version: $('version').value.trim(),
    });
}

/* ---------- 主操作按钮 ---------- */
$('btnInstall').addEventListener('click', () => send('install'));
$('btnDisable').addEventListener('click', () => send('disable'));
$('btnEnable').addEventListener('click', () => send('enable'));

let uninstallArmed = false, uninstallTimer = null;
$('btnUninstall').addEventListener('click', () => {
    if (!uninstallArmed) {
        uninstallArmed = true;
        $('btnUninstall').textContent = '再点一次确认卸载';
        uninstallTimer = setTimeout(() => {
            uninstallArmed = false;
            $('btnUninstall').textContent = '卸载';
        }, 3000);
        return;
    }
    clearTimeout(uninstallTimer);
    uninstallArmed = false;
    $('btnUninstall').textContent = '卸载';
    send('uninstall');
});

$('btnDlMqga').addEventListener('click', () => send('download', { host: 'mqga' }));
$('btnDlLiteloader').addEventListener('click', () => send('download', { host: 'liteloader' }));
$('btnLocate').addEventListener('click', () => send('locateQq'));

$('btnOpenHosts').addEventListener('click', () => send('openFolder', { path: '__hosts__' }));
$('btnUseDownloaded').addEventListener('click', () => send('getHostPath', { host: selectedHost }));

$('btnClearLog').addEventListener('click', () => { $('log').textContent = ''; });
$('btnOpenLog').addEventListener('click', () => send('openFolder', { path: '__data__' }));

$('btnLaunchQq').addEventListener('click', () => send('launchQq'));
$('linkKillQq').addEventListener('click', e => {
    e.preventDefault();
    send('killQq');
});

$('btnReinject').addEventListener('click', () => send('install'));

/* ---------- 初始化 ---------- */
if (!$('installDir').value) $('installDir').value = 'D:\\LiteLoaderQQNT';
log('注入器已就绪');
setTimeout(() => {
    ['hostSourceDir', 'installDir', 'qqRoot'].forEach(validateField);
    if ($('qqRoot').value.trim()) { send('listVersions'); checkUpdate(); }
}, 1500);
window.chrome.webview.postMessage({ action: 'ready' });
