// search.js — Search tab: query the KB with vector/hybrid/rerank/full modes.

import { callTool, toast, escapeHtml } from './api.js';

export async function renderSearch(panel) {
    panel.innerHTML = `
        <div class="row" style="margin-bottom: 16px;">
            <h2 style="margin: 0;">Search</h2>
            <span class="spacer"></span>
            <button id="search-clear">Clear</button>
        </div>

        <div class="card">
            <form id="search-form">
                <div class="field">
                    <label for="search-query">Query</label>
                    <input type="text" id="search-query" placeholder="Ask anything about your KB…" style="width: 100%;" autofocus>
                </div>
                <div class="row">
                    <div>
                        <label for="search-mode">Mode</label>
                        <select id="search-mode">
                            <option value="vector">vector — pure semantic</option>
                            <option value="hybrid" selected>hybrid — vector + BM25 + graph (RRF)</option>
                            <option value="rerank">rerank — hybrid then LLM</option>
                            <option value="full">full — hybrid + rerank</option>
                        </select>
                    </div>
                    <div>
                        <label for="search-topk">Top K</label>
                        <input type="number" id="search-topk" min="1" max="100" value="10" style="width: 80px;">
                    </div>
                    <span class="spacer"></span>
                    <button type="submit" id="search-go" class="primary">Search</button>
                </div>
            </form>
        </div>

        <div id="search-status" class="muted" style="margin: 8px 0;"></div>
        <div id="search-results"></div>
    `;

    document.getElementById('search-form').addEventListener('submit', onSearch);
    document.getElementById('search-clear').addEventListener('click', () => {
        document.getElementById('search-query').value = '';
        document.getElementById('search-results').innerHTML = '';
        document.getElementById('search-status').textContent = '';
    });

    document.getElementById('search-query').focus();
}

async function onSearch(e) {
    e.preventDefault();
    const query = document.getElementById('search-query').value.trim();
    const mode  = document.getElementById('search-mode').value;
    // Empty/garbage Top-K → NaN → would serialize as null; clamp instead.
    let topK = parseInt(document.getElementById('search-topk').value, 10);
    if (!Number.isFinite(topK) || topK < 1) topK = 10;
    if (topK > 100) topK = 100;
    if (!query) return;

    const go = document.getElementById('search-go');
    const status = document.getElementById('search-status');
    const results = document.getElementById('search-results');
    results.innerHTML = '';
    go.disabled = true;
    status.innerHTML = '<span class="spinner"></span> Searching…';

    try {
        const t0 = performance.now();
        // The apollo_query_kb tool expects query, mode, top_k.
        const data = await callTool('apollo_query_kb', { query, mode, top_k: topK });
        const ms = (performance.now() - t0).toFixed(0);

        // The tool returns an array of {chunk_id, content, document_title, scores?, source?}.
        const hits = Array.isArray(data) ? data
                   : Array.isArray(data?.results) ? data.results
                   : Array.isArray(data?.chunks) ? data.chunks
                   : [];
        status.textContent = `${hits.length} results in ${ms} ms`;

        if (hits.length === 0) {
            results.innerHTML = `
                <div class="empty">
                    <h3>No hits</h3>
                    <p>Try a different query or mode.</p>
                </div>`;
        } else {
            results.innerHTML = hits.map(renderCard).join('');
            // Wire up "Show more" expanders.
            results.querySelectorAll('.result-expand').forEach(btn => {
                btn.addEventListener('click', (ev) => {
                    const card = ev.target.closest('.result-card');
                    if (card) card.classList.toggle('expanded');
                });
            });
            results.querySelectorAll('.copy-id').forEach(btn => {
                btn.addEventListener('click', (ev) => {
                    const id = ev.target.dataset.id;
                    navigator.clipboard.writeText(id).then(() => toast(`Copied ${id}`));
                });
            });
        }
    } catch (err) {
        console.error(err);
        status.textContent = '';
        results.innerHTML = `
            <div class="empty">
                <h3>Search failed</h3>
                <p>${escapeHtml(err.message)}</p>
                <p class="muted">Tip: an embedding model needs to be loaded on the
                    <a href="#settings">Settings</a> tab before search can run.
                    (The knowledge base itself is always open at the
                    location the operator set at startup.)</p>
            </div>`;
    } finally {
        go.disabled = false;
    }
}

function renderCard(hit, idx) {
    const id   = hit.chunk_id ?? hit.id ?? `result-${idx}`;
    const docTitle = hit.document_title ?? hit.title ?? hit.source ?? '(unknown source)';
    const content = hit.content ?? hit.text ?? hit.chunk ?? '';
    const scores  = hit.scores ?? {};
    const finalScore = hit.score ?? hit.rrf ?? hit.hybrid_score ?? '';

    // Build score badges.
    const badges = [];
    if (scores.vector != null) badges.push(scoreBadge('vec', scores.vector));
    if (scores.bm25   != null) badges.push(scoreBadge('bm25', scores.bm25));
    if (scores.graph  != null) badges.push(scoreBadge('graph', scores.graph));
    if (finalScore !== '')     badges.push(scoreBadge('rrf', finalScore, true));

    return `
        <div class="result-card">
            <div class="meta">
                <span class="badge" data-id="${escapeHtml(id)}">#${idx + 1}</span>
                <button class="copy-id btn" data-id="${escapeHtml(id)}" style="padding: 1px 6px; font-size: 11px;">copy id</button>
                <span class="mono">${escapeHtml(id)}</span>
                <span class="muted">— ${escapeHtml(docTitle)}</span>
                <span class="spacer"></span>
                ${badges.join(' ')}
            </div>
            <div class="content">${escapeHtml(content)}</div>
            <div class="actions">
                <button class="result-expand">Show full</button>
            </div>
        </div>
    `;
}

function scoreBadge(label, score, primary = false) {
    let cls = 'badge';
    if (primary) cls += ' badge-ok';
    return `<span class="${cls} mono">${label} ${Number(score).toFixed(3)}</span>`;
}