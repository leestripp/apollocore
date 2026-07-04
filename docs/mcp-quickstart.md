# ApolloCore MCP Quickstart

A full worked session showing how to drive `apollo_mcp_server` from any
MCP-aware agent (or from `curl` for testing).

**Spec:** MCP 2025-11-25 (Streamable HTTP transport).
**Server default port:** 8765.

---

## 1. Start the server

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc) --target apollo_mcp_server

./build/src/mcp_server/apollo_mcp_server --port 8765 \
    --data-dir data/quickstart \
    --embed-model models/nomic-embed-text-v1.5.Q4_K_M.gguf
```

Expected output (last lines):

```
[ApolloCore] Embedding model loaded: models/nomic-embed-text-v1.5.Q4_K_M.gguf
[HttpServer] Starting server on 0.0.0.0:8765
[HttpServer] Socket bound and listening
[HttpServer] Accept loop started
```

---

## 2. Initialize (every MCP session starts with this)

```bash
curl -s -X POST http://127.0.0.1:8765/mcp \
  -H 'Content-Type: application/json' -H 'Accept: application/json' \
  -d '{
    "jsonrpc":"2.0","id":1,"method":"initialize",
    "params":{
      "protocolVersion":"2025-11-25",
      "capabilities":{},
      "clientInfo":{"name":"my-agent","version":"1.0"}
    }
  }'
```

```json
{
  "id":1,"jsonrpc":"2.0",
  "result":{
    "protocolVersion":"2025-11-25",
    "serverInfo":{"name":"apollo_mcp_server","version":"1.0.0"},
    "capabilities":{"tools":{},"resources":{"subscribe":false},"prompts":{},"logging":{}},
    "instructions":"ApolloCore is a local-first RAG knowledge base. Use
      apollo_query_kb to search, apollo_add_document to ingest text,
      apollo_ingest_codebase for code directories, and apollo_search_symbols
      for code symbol lookup."
  }
}
```

The server may return an `Mcp-Session-Id` header on this response. **Send it
back on every subsequent request** to keep using the same server-side session.

Then send the `initialized` notification (no response expected):

```bash
curl -s -X POST http://127.0.0.1:8765/mcp \
  -H 'Content-Type: application/json' -H 'Accept: application/json' \
  -d '{"jsonrpc":"2.0","method":"notifications/initialized"}'
```

---

## 3. Discover the surface

List tools:

```bash
curl -s -X POST http://127.0.0.1:8765/mcp \
  -H 'Content-Type: application/json' -H 'Accept: application/json' \
  -d '{"jsonrpc":"2.0","id":2,"method":"tools/list"}' | jq '.result.tools | length'
# → 9
```

List resources:

```bash
curl -s -X POST http://127.0.0.1:8765/mcp \
  -H 'Content-Type: application/json' -H 'Accept: application/json' \
  -d '{"jsonrpc":"2.0","id":3,"method":"resources/list"}' | jq '.result.resources[].uri'
```

List prompts:

```bash
curl -s -X POST http://127.0.0.1:8765/mcp \
  -H 'Content-Type: application/json' -H 'Accept: application/json' \
  -d '{"jsonrpc":"2.0","id":4,"method":"prompts/list"}' | jq '.result.prompts[].name'
# → "summarize_kb" "answer_with_citations" "code_review_summary" "entity_research"
```

## 4. Add a document

```bash
curl -s -X POST http://127.0.0.1:8765/mcp \
  -H 'Content-Type: application/json' -H 'Accept: application/json' \
  -d '{
    "jsonrpc":"2.0","id":5,"method":"tools/call",
              "arguments":{
                "text":"ApolloCore is a C++23 RAG library. It supports vector search, BM25 keyword search, knowledge graphs, and LLM-powered query enhancement. The project is built on llama.cpp.",
                "doc_id":"intro","chunk_size":512
              }}
  }'
```

```json
{"id":5,"jsonrpc":"2.0",
 "result":{"content":[{"type":"text","text":"Added 1 chunk(s)."}]}}
 "result":{"content":[{"type":"text","text":"Added 1 chunk(s)."}]}}
```

---

## 5. Query the KB
    "jsonrpc":"2.0","id":6,"method":"tools/call",
    "params":{"name":"apollo_query_kb",
              "arguments":{"query":"What is ApolloCore?","mode":"hybrid","top_k":3}}
  }'
```

```json
{"id":6,"jsonrpc":"2.0",
 "result":{"content":[{"type":"text",
   "text":"Hybrid search returned 3 result(s):\n  [1] (score 0.5, chunk ...)\n      ApolloCore is a C++23 RAG library. ..."}]}}
```bash
curl -s -X POST http://127.0.0.1:8765/mcp \
  -H 'Content-Type: application/json' -H 'Accept: application/json' \
  -d '{
    "jsonrpc":"2.0","id":7,"method":"tools/call",
    "params":{"name":"apollo_query_kb",
              "arguments":{"query":"What is ApolloCore?","mode":"hybrid","top_k":3}}
  }'
```

```json
{"id":7,"jsonrpc":"2.0",
 "result":{"content":[{"type":"text",
   "text":"Hybrid search returned 3 result(s):\n  [1] (score 0.5, chunk ...)\n      ApolloCore is a C++23 RAG library. ..."}]}}
```

`mode` accepts `vector`, `hybrid`, `rerank`, `full`.

---

## 6. Read a resource

```bash
curl -s -X POST http://127.0.0.1:8765/mcp \
  -H 'Content-Type: application/json' -H 'Accept: application/json' \
  -d '{
    "jsonrpc":"2.0","id":7,"method":"resources/read",
    "params":{"uri":"apollo://stats"}
  }'
```

```json
{"id":7,"jsonrpc":"2.0",
 "result":{"contents":[{"mimeType":"application/json","uri":"apollo://stats",
   "text":"{\n  \"chunks\": 1,\n  \"documents\": 1,\n  \"entities\": 0,\n  \"relationships\": 0\n}"}]}}
```

---

## 7. Get a prompt

```bash
curl -s -X POST http://127.0.0.1:8765/mcp \
  -H 'Content-Type: application/json' -H 'Accept: application/json' \
  -d '{
    "jsonrpc":"2.0","id":8,"method":"prompts/get",
    "params":{"name":"answer_with_citations","arguments":{"question":"What is ApolloCore?"}}
  }'
```

The response contains a `messages` array — feed those messages into the
LLM as the conversation's user turn.

---

## 8. Configure your agent

For Claude Desktop, edit `~/.config/Claude/claude_desktop_config.json`:

```json
{
  "mcpServers": {
    "apollocore": {
      "url": "http://127.0.0.1:8765/mcp",
      "transport": "streamable-http"
    }
  }
}
```

Restart Claude Desktop. In any new conversation, the 9 `apollo_*` tools
will appear automatically. The agent can now answer questions, ingest
documents, and walk codebases by calling them.

For Cursor: **Settings → MCP → Add new server → URL =
http://127.0.0.1:8765/mcp**.

For Cline / Continue / Windsurf: similar — consult their docs for
"MCP server URL".

---

## 9. Health check

For docker/k8s liveness probes (not an MCP method — pure HTTP):

```bash
curl -s http://127.0.0.1:8765/health | jq
```

```json
{
  "status":"ok",
  "name":"apollo_mcp_server",
  "version":"1.0.0",
  "protocol":"2025-11-25",
  "tools":9, "resources":5, "prompts":4,
  "sessions":0, "documents":0, "chunks":0, "entities":0
}
```

---

## Troubleshooting

**"Accept must include application/json"** — your curl request needs
`-H 'Accept: application/json'`. Some MCP clients send this by default;
others don't.

**No models loaded → `apollo_query_kb` errors with "embeddings not available"** —
ensure `APOLLO_EMBED_MODEL` points to a real `.gguf` file and that
`load_embedding_model` returned `true` in the server log.

**Server-side logging** — `~/.omninet/logs/apollo_mcp_server.log` (override
with `OMNINET_DATA_DIR`).

**`Mcp-Session-Id` mismatches** — the server generates one per first
contact and remembers it. If you forget to send it back, the server
will create a new session on the next call (which is fine — sessions are
mostly used for multi-step elicitation patterns, not for state).