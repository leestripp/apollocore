# Deploying ApolloCore

`apollo_mcp_server` is a single self-contained executable. There is no
installer or packaging in this repo — build it, then run the binary
directly, under your own supervisor if you need one (a hand-written
systemd unit, tmux, a CI wrapper, …).

---

## 1. Build

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

## 2. Run

```bash
./build/src/mcp_server/apollo_mcp_server --port 8765
```

Verify:

```bash
curl -s http://localhost:8765/health     # JSON, status:"ok"
```

Open `http://localhost:8765/` in a browser to see the Web UI.

Useful flags / env vars (all optional):

| Flag / env var | Default | Meaning |
|----------------|---------|---------|
| `--host` / `APOLLO_HOST` | `0.0.0.0` | bind address |
| `--port` / `APOLLO_PORT` | `8765` | listen port |
| `--data-dir` / `APOLLO_DATA_DIR` | `data` | KB root directory |
| `--embed-model` / `APOLLO_EMBED_MODEL` | `models/nomic-embed-text-v1.5.Q4_K_M.gguf` | embedding GGUF |
| `--llm-model` / `APOLLO_LLM_MODEL` | `models/qwen2.5-0.5b-instruct-q4_k_m.gguf` | LLM GGUF (optional) |
| `--no-ui` / `APOLLO_NO_UI` | off | serve only JSON-RPC, no dashboard |
| `--admin` | off | mount OmniNet admin dashboard at `/omninet` |
| `-v` / `--verbose` | off | debug logging |

## 3. Connect an MCP client

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

## 4. Exposing on a network

The server runs **without authentication by default** and is designed for
loopback use. If you expose it on a LAN/internet:

1. Set `APOLLO_AUTH_TOKEN` to a strong random secret (32+ chars) — clients
   send it as an RFC 6750 Bearer token.
2. Pass `--host 0.0.0.0` to bind on the network interface.
3. Front it with a TLS reverse proxy (Caddy/Nginx) for HTTPS — proxy
   `localhost:8765` only.
4. There is no rate limiting and no multi-tenant isolation.

## 5. Running as a service

Write your own unit / supervisor entry around the binary. A minimal
systemd example:

```ini
[Unit]
Description=ApolloCore MCP server
After=network.target

[Service]
ExecStart=/path/to/build/src/mcp_server/apollo_mcp_server --port 8765
WorkingDirectory=/path/to/ApolloCore
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
```

Hardening (`ProtectSystem=strict`, `MemoryDenyWriteExecute`,
`NoNewPrivileges`, `PrivateTmp`, …) is recommended for any networked
deployment — the server needs a writable `--data-dir`, a TCP port, and
nothing else.

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| `Address already in use` on startup | another process holds the port; `ss -ltnp 'sport = :8765'`, then change `--port` |
| Embedding model missing | download the GGUFs into `models/` (see `README.md` → Models) or pass `--embed-model` |
| `no backends are loaded` on model load | stale llama.cpp build — reconfigure from a clean build dir so the CMake patch in `CMakeLists.txt` re-applies |
| Want only JSON-RPC, no UI | `--no-ui` |
