// graph.js — Graph viewer: hand-rolled Fruchterman-Reingold layout on canvas.

import { readResource, toast, escapeHtml } from './api.js';

// Hard cap on rendered nodes. Fruchterman-Reingold is O(n²) per step, so
// beyond ~1500 nodes the upfront layout alone would freeze the tab.
const MAX_NODES = 1500;

const TYPE_COLORS = {
    PERSON:               '#58a6ff',
    ORGANIZATION:         '#f78166',
    LOCATION:             '#3fb950',
    EVENT:                '#d29922',
    CONCEPT:              '#a371f7',
    PRODUCT:              '#ff7b72',
    DATE:                 '#7d8590',
    NUMBER:               '#79c0ff',
    DISEASE:              '#db61a2',
    DRUG:                 '#db61a2',
    MEDICAL_PROCEDURE:    '#db61a2',
    SCIENTIFIC_TERM:      '#a371f7',
    FUNCTION:             '#e3b341',
    CLASS:                '#e3b341',
    FILE:                 '#8b949e',
    MODULE:               '#8b949e',
    NAMESPACE:            '#8b949e',
    VARIABLE:             '#7d8590',
    ENUM:                 '#e3b341',
    MACRO:                '#e3b341',
    UNKNOWN:              '#7d8590',
};

function colorForType(type) {
    return TYPE_COLORS[type] ?? TYPE_COLORS.UNKNOWN;
}

// Fruchterman-Reingold. O(n²) per iteration; fine for ≤500 nodes.
function frStep(nodes, edges, area, k, temperature) {
    // Repulsive forces (between every pair).
    const disp = nodes.map(() => ({ dx: 0, dy: 0 }));
    for (let i = 0; i < nodes.length; ++i) {
        for (let j = i + 1; j < nodes.length; ++j) {
            const a = nodes[i], b = nodes[j];
            let dx = a.x - b.x, dy = a.y - b.y;
            let dist = Math.sqrt(dx*dx + dy*dy) || 0.01;
            const rep = (k * k) / dist;
            const ux = dx / dist, uy = dy / dist;
            disp[i].dx += ux * rep; disp[i].dy += uy * rep;
            disp[j].dx -= ux * rep; disp[j].dy -= uy * rep;
        }
    }
    // Attractive forces (along edges).
    for (const e of edges) {
        const a = nodes[e.si], b = nodes[e.ti];
        let dx = a.x - b.x, dy = a.y - b.y;
        let dist = Math.sqrt(dx*dx + dy*dy) || 0.01;
        const attr = (dist * dist) / k;
        const ux = dx / dist, uy = dy / dist;
        disp[e.si].dx += ux * attr; disp[e.si].dy += uy * attr;
        disp[e.ti].dx -= ux * attr; disp[e.ti].dy -= uy * attr;
    }
    // Apply displacement, capped by temperature.
    for (let i = 0; i < nodes.length; ++i) {
        const n = nodes[i];
        if (n.pinned) continue;
        const d = disp[i];
        const mag = Math.sqrt(d.dx*d.dx + d.dy*d.dy) || 0.01;
        n.x += (d.dx / mag) * Math.min(mag, temperature);
        n.y += (d.dy / mag) * Math.min(mag, temperature);
        // Clamp to area.
        const m = 8;
        n.x = Math.max(m, Math.min(area.w - m, n.x));
        n.y = Math.max(m, Math.min(area.h - m, n.y));
    }
}

export async function renderGraph(panel) {
    panel.innerHTML = `
        <div class="row" style="margin-bottom: 16px;">
            <h2 style="margin: 0;">Graph</h2>
            <span class="spacer"></span>
            <span id="graph-status" class="muted"></span>
            <button id="graph-reload">Reload</button>
            <button id="graph-pause">Pause</button>
            <button id="graph-reset">Reset zoom</button>
        </div>

        <div class="graph-toolbar">
            <input type="text" id="graph-filter" placeholder="Filter by name…" style="flex: 1; max-width: 320px;">
            <div id="graph-type-filters" class="row"></div>
        </div>

        <div class="graph-wrap">
            <canvas id="graph-canvas"></canvas>
            <div class="graph-detail" id="graph-detail"></div>
        </div>
    `;

    const canvas = document.getElementById('graph-canvas');
    const ctx = canvas.getContext('2d');

    // Fit canvas to its container.
    function resize() {
        const rect = canvas.parentElement.getBoundingClientRect();
        const dpr = window.devicePixelRatio || 1;
        canvas.width  = rect.width * dpr;
        canvas.height = rect.height * dpr;
        canvas.style.width  = rect.width + 'px';
        canvas.style.height = rect.height + 'px';
        ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    }
    resize();
    window.addEventListener('resize', resize);

    let nodes = [], edges = [];
    let view = { x: 0, y: 0, scale: 1 };
    let dragging = null;
    let paused = false;
    let hoverNode = null;
    let typeFilters = new Set();
    let truncatedFrom = 0;
    // Simulation "heat": 1 = full jitter, decays toward 0 as the layout
    // settles. The sim sleeps at 0 (no O(n²) work per frame, no eternal
    // jiggle) and re-heats on reload or when the user drags a node.
    let heat = 1;

    document.getElementById('graph-reload').addEventListener('click', load);
    document.getElementById('graph-pause').addEventListener('click', (e) => {
        paused = !paused;
        e.target.textContent = paused ? 'Resume' : 'Pause';
    });
    document.getElementById('graph-reset').addEventListener('click', () => {
        view = { x: 0, y: 0, scale: 1 };
        fitView();
    });
    document.getElementById('graph-filter').addEventListener('input', filterByName);

    // Wheel zoom.
    canvas.addEventListener('wheel', (e) => {
        e.preventDefault();
        const factor = e.deltaY < 0 ? 1.1 : 1 / 1.1;
        const rect = canvas.getBoundingClientRect();
        const cx = e.clientX - rect.left, cy = e.clientY - rect.top;
        view.x = cx - (cx - view.x) * factor;
        view.y = cy - (cy - view.y) * factor;
        view.scale *= factor;
        view.scale = Math.max(0.1, Math.min(8, view.scale));
    }, { passive: false });

    // Pan + node drag.
    canvas.addEventListener('pointerdown', (e) => {
        const p = screenToWorld(e);
        const hit = pickNode(p.x, p.y);
        if (hit) {
            dragging = { node: hit, mode: 'node', offX: hit.x - p.x, offY: hit.y - p.y };
            hit.pinned = true;
            // Dragging a node stirs its neighbourhood — re-heat a little.
            heat = Math.max(heat, 0.3);
        } else {
            dragging = { mode: 'pan', lastX: e.clientX, lastY: e.clientY };
        }
        canvas.setPointerCapture(e.pointerId);
    });
    canvas.addEventListener('pointermove', (e) => {
        if (!dragging) {
            const p = screenToWorld(e);
            hoverNode = pickNode(p.x, p.y);
            canvas.style.cursor = hoverNode ? 'pointer' : 'grab';
            return;
        }
        if (dragging.mode === 'node') {
            const p = screenToWorld(e);
            dragging.node.x = p.x + dragging.offX;
            dragging.node.y = p.y + dragging.offY;
        } else {
            const dx = e.clientX - dragging.lastX;
            const dy = e.clientY - dragging.lastY;
            view.x += dx; view.y += dy;
            dragging.lastX = e.clientX; dragging.lastY = e.clientY;
        }
    });
    canvas.addEventListener('pointerup', (e) => {
        if (dragging?.mode === 'node') {
            // Double-click detection handled separately.
        }
        dragging = null;
    });
    canvas.addEventListener('dblclick', (e) => {
        const p = screenToWorld(e);
        const hit = pickNode(p.x, p.y);
        if (hit) {
            hit.pinned = false;
        }
    });
    canvas.addEventListener('click', (e) => {
        const p = screenToWorld(e);
        const hit = pickNode(p.x, p.y);
        if (hit) showDetail(hit);
        else      hideDetail();
    });

    async function load() {
        const status = document.getElementById('graph-status');
        status.innerHTML = '<span class="spinner"></span> loading graph…';
        try {
            const data = await readResource('apollo://graph');
            ingest(data);
            initLayout();
            rebuildTypeFilters();
            status.textContent = `${nodes.length} nodes, ${edges.length} edges`
                + (truncatedFrom ? ` (capped from ${truncatedFrom})` : '');
        } catch (err) {
            status.textContent = '';
            toast(`Graph load failed: ${err.message}`, 'err');
            panel.querySelector('.graph-wrap').innerHTML = `
                <div class="empty">
                    <h3>Graph unavailable</h3>
                    <p>${escapeHtml(err.message)}</p>
                </div>`;
        }
    }

    function ingest(data) {
        let rawNodes = data.nodes ?? data.entities ?? [];
        const rawEdges = data.edges ?? data.relationships ?? [];

        // Cap node count so the O(n²) layout stays interactive. Keep the
        // highest-degree nodes: sort by connectivity first requires edges,
        // so we approximate by order (server emits important entities
        // first) and just truncate, noting it in the status line.
        truncatedFrom = 0;
        if (rawNodes.length > MAX_NODES) {
            truncatedFrom = rawNodes.length;
            rawNodes = rawNodes.slice(0, MAX_NODES);
        }

        const idToIdx = new Map();
        nodes = rawNodes.map((n, i) => {
            const id = n.id ?? n.name ?? `n${i}`;
            idToIdx.set(id, i);
            return {
                id,
                idx: i,
                name: n.name ?? id,
                type: n.type ?? 'UNKNOWN',
                description: n.description ?? '',
                degree: 0,
                x: Math.random() * 800,
                y: Math.random() * 600,
                vx: 0, vy: 0,
                pinned: false,
            };
        });

        edges = [];
        for (const e of rawEdges) {
            const s = idToIdx.get(e.source ?? e.from);
            const t = idToIdx.get(e.target ?? e.to);
            if (s === undefined || t === undefined || s === t) continue;
            edges.push({ si: s, ti: t, type: e.type ?? '', label: e.type ?? '' });
            nodes[s].degree++; nodes[t].degree++;
        }
    }

    function initLayout() {
        const rect = canvas.parentElement.getBoundingClientRect();
        const area = { w: rect.width, h: rect.height };
        const k = Math.sqrt(area.w * area.h / (nodes.length || 1));
        // Run 300 iterations up-front so the user doesn't see a blob.
        let temperature = area.w / 10;
        for (let i = 0; i < 300; ++i) {
            frStep(nodes, edges, area, k, temperature);
            temperature *= 0.97;
        }
        heat = 1;  // fresh layout — let the live sim settle from hot
        fitView();
    }

    function fitView() {
        if (nodes.length === 0) return;
        const xs = nodes.map(n => n.x), ys = nodes.map(n => n.y);
        const minX = Math.min(...xs), maxX = Math.max(...xs);
        const minY = Math.min(...ys), maxY = Math.max(...ys);
        const rect = canvas.parentElement.getBoundingClientRect();
        const pad = 40;
        const scale = Math.min(
            (rect.width - 2*pad) / Math.max(1, maxX - minX),
            (rect.height - 2*pad) / Math.max(1, maxY - minY),
            1.5,
        );
        view.scale = scale;
        view.x = pad - minX * scale + (rect.width - (maxX - minX) * scale) / 2;
        view.y = pad - minY * scale + (rect.height - (maxY - minY) * scale) / 2;
    }

    function screenToWorld(e) {
        const rect = canvas.getBoundingClientRect();
        const sx = e.clientX - rect.left, sy = e.clientY - rect.top;
        return { x: (sx - view.x) / view.scale, y: (sy - view.y) / view.scale };
    }
    function pickNode(wx, wy) {
        // Pick in reverse so nodes on top win.
        for (let i = nodes.length - 1; i >= 0; --i) {
            const n = nodes[i];
            const r = radiusFor(n) + 2;
            const dx = wx - n.x, dy = wy - n.y;
            if (dx*dx + dy*dy <= r*r) return n;
        }
        return null;
    }

    function radiusFor(n) {
        return 6 + Math.log(1 + (n.degree || 0)) * 2.5;
    }

    function rebuildTypeFilters() {
        const types = Array.from(new Set(nodes.map(n => n.type))).sort();
        const host = document.getElementById('graph-type-filters');
        host.innerHTML = '';
        typeFilters = new Set(types);
        for (const t of types) {
            const chip = document.createElement('span');
            chip.className = 'badge';
            chip.style.cursor = 'pointer';
            chip.style.borderColor = colorForType(t);
            chip.style.color = colorForType(t);
            chip.textContent = `${t} (${nodes.filter(n => n.type === t).length})`;
            chip.addEventListener('click', () => {
                if (typeFilters.has(t)) typeFilters.delete(t);
                else                   typeFilters.add(t);
                chip.style.opacity = typeFilters.has(t) ? '1' : '0.3';
            });
            host.appendChild(chip);
        }
    }

    let currentFilter = '';
    function filterByName(e) {
        currentFilter = e.target.value.trim().toLowerCase();
    }

    function nodeMatches(n) {
        if (!typeFilters.has(n.type)) return false;
        if (currentFilter && !n.name.toLowerCase().includes(currentFilter)) return false;
        return true;
    }

    function showDetail(n) {
        const el = document.getElementById('graph-detail');
        el.classList.add('shown');
        const neighbors = edges
            .filter(e => e.si === n.idx || e.ti === n.idx)
            .map(e => {
                const other = e.si === n.idx ? nodes[e.ti] : nodes[e.si];
                return `${escapeHtml(other.name)} <span class="muted">— ${escapeHtml(e.type || 'relates')}</span>`;
            })
            .join('<br>') || '<span class="muted">(no neighbors)</span>';
        el.innerHTML = `
            <h3 style="margin-top: 0;">${escapeHtml(n.name)}</h3>
            <p><span class="badge" style="border-color: ${colorForType(n.type)}; color: ${colorForType(n.type)};">${escapeHtml(n.type)}</span></p>
            ${n.description ? `<p>${escapeHtml(n.description)}</p>` : ''}
            <h4>Neighbors</h4>
            <div style="font-size: 12px;">${neighbors}</div>
            <p class="muted" style="margin-top: 12px;">Drag to pin • Double-click to unpin</p>
        `;
    }
    function hideDetail() {
        document.getElementById('graph-detail').classList.remove('shown');
    }

    // Animation loop.
    let raf;
    function tick() {
        const rect = canvas.parentElement.getBoundingClientRect();
        const area = { w: rect.width, h: rect.height };
        const k = Math.sqrt(area.w * area.h / (nodes.length || 1));
        // Cool the simulation: constant temperature meant the graph
        // jittered forever and burned CPU on an O(n²) step every frame.
        // Below the threshold the layout is settled — skip the physics
        // entirely (also when the browser tab is hidden).
        if (!paused && heat > 0.005 && !document.hidden) {
            const temperature = (area.w / 60) * heat;
            frStep(nodes, edges, area, k, temperature);
            heat *= 0.98;
        }
        draw();
        raf = requestAnimationFrame(tick);
    }

    function draw() {
        const rect = canvas.parentElement.getBoundingClientRect();
        ctx.clearRect(0, 0, rect.width, rect.height);

        ctx.save();
        ctx.translate(view.x, view.y);
        ctx.scale(view.scale, view.scale);

        // Edges.
        ctx.lineWidth = 1 / view.scale;
        for (const e of edges) {
            const a = nodes[e.si], b = nodes[e.ti];
            if (!nodeMatches(a) || !nodeMatches(b)) continue;
            const isHighlight = hoverNode && (a === hoverNode || b === hoverNode);
            ctx.strokeStyle = isHighlight ? 'rgba(88, 166, 255, 0.9)' : 'rgba(125, 133, 144, 0.35)';
            ctx.beginPath();
            ctx.moveTo(a.x, a.y);
            ctx.lineTo(b.x, b.y);
            ctx.stroke();
        }

        // Nodes.
        for (const n of nodes) {
            if (!nodeMatches(n)) continue;
            const r = radiusFor(n);
            const isHover = n === hoverNode;
            const isPinned = n.pinned;

            ctx.fillStyle = colorForType(n.type);
            ctx.beginPath();
            ctx.arc(n.x, n.y, r, 0, Math.PI * 2);
            ctx.fill();

            if (isHover || isPinned) {
                ctx.strokeStyle = isPinned ? '#f78166' : '#fff';
                ctx.lineWidth = (isPinned ? 2.5 : 1.5) / view.scale;
                ctx.stroke();
            }

            if (isHover || view.scale > 0.9) {
                ctx.fillStyle = '#e6edf3';
                ctx.font = `${Math.max(10, 12 / view.scale)}px ${getComputedStyle(document.body).fontFamily}`;
                ctx.textBaseline = 'middle';
                ctx.textAlign = 'left';
                ctx.fillText(n.name, n.x + r + 3, n.y);
            }
        }

        ctx.restore();
    }

    await load();
    raf = requestAnimationFrame(tick);

    return () => {
        cancelAnimationFrame(raf);
        window.removeEventListener('resize', resize);
    };
}