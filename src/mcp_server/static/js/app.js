// app.js — tab switching + module loading + global error handling

import { initialize, toast } from './api.js';
import { renderHealth }   from './health.js';
import { renderSearch }   from './search.js';
import { renderGraph }    from './graph.js';
import { renderSettings } from './settings.js';
import { renderAbout }    from './about.js';

const TABS = {
    health:   { render: renderHealth,   panel: 'tab-health' },
    search:   { render: renderSearch,   panel: 'tab-search' },
    graph:    { render: renderGraph,    panel: 'tab-graph' },
    settings: { render: renderSettings, panel: 'tab-settings' },
    about:    { render: renderAbout,    panel: 'tab-about' },
};

const TAB_ORDER = ['health', 'search', 'graph', 'settings', 'about'];

/** Active timers/callbacks per tab so we can clean up on tab switch. */
const activeCleanups = new Map();

function getTabFromHash() {
    const h = window.location.hash.replace(/^#/, '');
    return TABS[h] ? h : 'health';
}

function highlightTab(name) {
    document.querySelectorAll('.tab').forEach(el => {
        el.classList.toggle('active', el.dataset.tab === name);
    });
}

async function showTab(name) {
    if (!TABS[name]) name = 'health';

    // Clean up the previous tab if it registered a teardown.
    const prev = activeCleanups.get(currentTab);
    if (prev?.cleanup) try { prev.cleanup(); } catch (e) { console.error(e); }

    // Hide all panels.
    document.querySelectorAll('.tab-panel').forEach(el => el.hidden = true);

    // Show the chosen one and call its renderer.
    const t = TABS[name];
    const panel = document.getElementById(t.panel);
    panel.hidden = false;
    highlightTab(name);
    currentTab = name;
    try {
        const cleanup = await t.render(panel);
        if (typeof cleanup === 'function') activeCleanups.set(name, { cleanup });
        else                                  activeCleanups.set(name, {});
    } catch (e) {
        console.error(e);
        toast(`Failed to render ${name}: ${e.message}`, 'err');
        panel.innerHTML = `<div class="empty"><h3>Render error</h3><p>${escapeHtml(e.message)}</p></div>`;
    }
}

function escapeHtml(s) {
    return String(s)
        .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;').replace(/'/g, '&#39;');
}

let currentTab = 'health';

// Wire tab clicks + hashchange.
document.querySelectorAll('.tab').forEach(el => {
    el.addEventListener('click', (e) => {
        e.preventDefault();
        const name = el.dataset.tab;
        window.location.hash = `#${name}`;
    });
});
window.addEventListener('hashchange', () => showTab(getTabFromHash()));

// Global error handler so an uncaught fetch error still surfaces.
window.addEventListener('unhandledrejection', (e) => {
    console.error(e.reason);
    toast(`Unhandled error: ${e.reason?.message ?? e.reason}`, 'err');
});

// Boot.
(async function () {
    try {
        await initialize();
    } catch (e) {
        console.warn('initialize failed (server may not be ready):', e);
        toast(`Server not ready: ${e.message}`, 'warn');
    }
    await showTab(getTabFromHash());
})();