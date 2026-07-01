# Deploying ApolloCore on Ubuntu 26.04

This is the end-to-end deployment guide for running `apollo_mcp_server`
as a long-lived systemd service on Ubuntu 26.04 LTS (or any distro with
systemd v255+). The same steps work on Debian 13 and recent Fedora.

If you're running this somewhere without systemd (distrobox, plain
containers, CI sandboxes), use [`scripts/start.sh`](#manual-fallback-no-systemd)
instead.

---

## 1. Quick install (one command)

```bash
sudo ./scripts/install.sh
```

That's it. The script:

1. Creates a system user `apollocore` (`/usr/sbin/nologin` shell).
2. Creates directories under `/opt/apollocore/`, `/var/lib/apollocore/`,
   `/var/log/apollocore/`, `/var/cache/apollocore/`.
3. Installs the binary and UI via `cmake --install`.
4. Copies the systemd unit + env file to `/etc/`.
5. Downloads the embedding + LLM GGUF models (~550 MB) if missing.
6. `systemctl daemon-reload && systemctl enable --now apollo_mcp_server`.

The service is now reachable on `http://<host>:8765/`.

Re-run the script any time to update (binary, models, UI).

## 2. Verify the install

```bash
systemctl status apollo_mcp_server       # should be active (running)
journalctl -u apollo_mcp_server -f       # live logs
curl -s http://localhost:8765/health     # JSON, status:"ok"
```

Open `http://localhost:8765/` in a browser to see the Web UI.

## 3. Configuration

Edit `/etc/default/apollocore` then `systemctl restart apollo_mcp_server`:

```bash
sudoedit /etc/default/apollocore
sudo systemctl restart apollo_mcp_server
```

Variables (all optional):

| Variable | Default | Meaning |
|----------|---------|---------|
| `APOLLO_HOST` | `0.0.0.0` | bind address |
| `APOLLO_PORT` | `8765` | listen port |
| `APOLLO_EMBED_MODEL` | `/opt/apollocore/models/nomic-embed-text-v1.5.Q4_K_M.gguf` | embedding GGUF |
| `APOLLO_LLM_MODEL` | `/opt/apollocore/models/qwen2.5-0.5b-instruct-q4_k_m.gguf` | LLM GGUF (optional) |
| `APOLLO_MEMORY_MAX` | `2G` | systemd `MemoryMax=` cap |
| `APOLLO_UI` | `1` | set to `0` to disable the dashboard |

## 4. Operational tasks

### Connect an MCP client

Any client speaking the Streamable HTTP transport works. Claude
Desktop example at `~/.config/Claude/claude_desktop_config.json`:

```json
{
  "mcpServers": {
    "apollocore": {
      "url": "http://localhost:8765/mcp",
      "transport": "streamable-http"
    }
  }
}
```

The same URL works for Cursor, Cline, Windsurf, Continue.dev.

### Browse the UI

```bash
xdg-open http://localhost:8765/
```

Five tabs: Health (auto-refresh), Search, Graph, Settings, About.
Full tour in [`docs/ui.md`](ui.md).

### Manage the KB

Tools exposed as MCP tools (also clickable in the UI):

- `apollo_create_kb` / `apollo_open_kb` / `apollo_save_kb`
- `apollo_set_project` / `apollo_list_projects`
- `apollo_add_document` / `apollo_query_kb`
- `apollo_ingest_codebase` / `apollo_extract_entities`

### Logs

```bash
journalctl -u apollo_mcp_server -f               # journald (primary)
tail -f /var/log/apollocore/apollo_mcp_server.log   # mirror (also via systemd)
```

### Restart / stop / status

```bash
sudo systemctl restart apollo_mcp_server
sudo systemctl stop    apollo_mcp_server
sudo systemctl status  apollo_mcp_server
```

The unit restarts on crash with `RestartSec=5`, so transient
failures (model load timeout, etc.) self-heal without intervention.

## 5. Hardening notes

The shipped unit is locked down by default:

- **`ProtectSystem=strict`** — `/usr`, `/boot`, `/etc` are read-only.
- **`ProtectHome=true`** — `/home`, `$HOME` invisible.
- **`PrivateTmp=true`** — `/tmp` is a private namespace.
- **`ReadWritePaths=`** — only `/var/lib/apollocore`,
  `/var/log/apollocore`, `/var/cache/apollocore` are writable.
- **`MemoryDenyWriteExecute=true`** — no W^X pages.
- **`RestrictNamespaces`**, **`RestrictRealtime`**, **`RestrictSUIDSGID`**,
  **`LockPersonality`**, **`ProtectKernelTunables`** — defence in depth.

The server still binds a TCP port (AF_INET) and creates `/tmp` files
(via OmniNet). All other namespaces and capabilities are dropped.

If you want to expose the server to the network behind a reverse
proxy, keep these settings as-is and proxy `localhost:8765` only —
do not bind the service to `0.0.0.0` directly without TLS.

## 6. Uninstall

Keep data, remove service:

```bash
sudo ./scripts/uninstall.sh
```

Remove everything (binary, models, data, system user):

```bash
sudo ./scripts/uninstall.sh --purge --purge-user
```

## 7. Manual fallback (no systemd)

If systemd isn't available (distrobox container, sandbox, etc.):

```bash
# Foreground
./scripts/start.sh

# Detached
./scripts/start.sh --detach
./scripts/stop.sh
```

Environment overrides:

```bash
APOLLO_PORT=9000 APOLLO_HOST=127.0.0.1 ./scripts/start.sh --no-ui --verbose
```

## 8. Build from source

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc) --target apollo_mcp_server
```

GPU backends:

```bash
cmake -S . -B build -DLLAMA_VULKAN=ON   # AMD / Intel
cmake -S . -B build -DLLAMA_CUBLAS=ON   # NVIDIA
cmake -S . -B build -DLLAMA_METAL=ON    # Apple
```

First configure takes ~5 min (llama.cpp + OmniNet + their transitive deps).
Subsequent builds are incremental.

---

## File map

```
/opt/apollocore/
├── bin/
│   └── apollo_mcp_server                ← the only executable
└── share/apollocore/ui/
    └── static/                          ← HTML/CSS/JS dashboard

/var/lib/apollocore/
└── data/                                ← SQLite + .fbin embeddings (per-KB subdirs)

/var/log/apollocore/
└── apollo_mcp_server.log                 ← mirror of journald

/var/cache/apollocore/                    ← OmniNet HTTP cache

/etc/
├── default/apollocore                    ← EnvironmentFile
└── systemd/system/apollo_mcp_server.service
```

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| `systemctl status` shows `failed (exit-code)` | `journalctl -u apollo_mcp_server -n 100` |
| `Address already in use` on startup | another process holds the port; `ss -ltnp 'sport = :8765'` then change `APOLLO_PORT` in `/etc/default/apollocore` |
| Embedding model missing | the install script auto-downloads; check `/var/log/apollocore/apollo_mcp_server.log` |
| High memory use | lower `APOLLO_MEMORY_MAX` in `/etc/default/apollocore` |
| Want only JSON-RPC, no UI | set `APOLLO_UI=0` |
| After editing `/etc/default/apollocore` | `sudo systemctl restart apollo_mcp_server` |
| Logs filling `/var/log` | journald handles rotation; tune via `/etc/logrotate.d/apollo_mcp_server` |