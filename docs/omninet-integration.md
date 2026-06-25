# OmniNet Integration

ApolloCore integrates [OmniNet](https://gitlab.com/leestripp/omninet) — a C++23
HTTP/WebSocket server library — as a private dependency for future HTTP-based
features (REST API, MCP HTTP transport, browser dashboard).

## How It's Integrated

OmniNet is fetched at configure time via CMake `FetchContent`:

```cmake
FetchContent_Declare(
    omninet
    GIT_REPOSITORY https://gitlab.com/leestripp/omninet.git
    GIT_TAG        0.7.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(omninet)
```

The `apollo_core` static library links against `OmniNet` as a **private**
dependency, so ApolloCore users do not get OmniNet as a transitive dependency.

## Requirements

- **OpenSSL 1.1.1+** (3.x recommended) — OmniNet requires it for TLS
- **SQLite3** — OmniNet uses it for sessions (ApolloCore already uses it for KB persistence)
- **C++23 compiler** — both libraries require GCC 13+ or Clang 17+

On Ubuntu/Debian:
```bash
sudo apt install -y libssl-dev zlib1g-dev libsqlite3-dev
```

## Build

OmniNet is fetched automatically on first configure. No manual setup needed.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

The first build will:
1. Download OmniNet source (~5MB)
2. Build OmniNet static library (~40 source files)
3. Build llama.cpp (existing dependency)
4. Build apollo_core (links both)

Subsequent builds are incremental and fast (~seconds).

## Hello World Example

`examples/omninet_hello.cpp` demonstrates the integration:

```bash
cmake --build build --target omninet_hello
./build/examples/omninet_hello
```

In another terminal:
```bash
curl http://localhost:18080/api/health
# {
#     "status": "ok",
#     "service": "apollocore",
#     "version": "1.0.0",
#     "transport": "omninet"
# }
```

## Using OmniNet in Your ApolloCore Code

Include the umbrella header:

```cpp
#include "OmniNet.h"

namespace on  = OmniNet;
namespace onh = OmniNet::HTTP;
```

Add to your target's `CMakeLists.txt`:
```cmake
target_link_libraries(my_app PRIVATE apollo_core)
# apollo_core already brings OmniNet transitively as PRIVATE
```

Or link directly (if you don't want apollo_core):
```cmake
target_link_libraries(my_app PRIVATE OmniNet)
```

## What OmniNet Gives ApolloCore

| Feature | Use in ApolloCore |
|---------|-------------------|
| HTTP/1.1 server | REST API for KB queries |
| WebSocket (RFC 6455) | Live search updates in browser UI |
| TLS/HTTPS | Remote access to local KB |
| Static file serving | Browser-based dashboard |
| SQLite sessions | Admin auth for browser UI |
| API keys | Auth for REST clients |
| CSRF + CORS helpers | Secure browser access |
| SSE (Server-Sent Events) | Stream long LLM responses |
| Admin dashboard | KB monitoring at `/omninet` |

## Roadmap (What's Next)

Phase O1 (this phase) just verifies the integration builds. Future phases:

- **Phase M1.16** — MCP HTTP transport (the AI calls ApolloCore over HTTP/SSE)
- **Phase REST** — REST API for KB operations (`POST /api/search`, `POST /api/add`, etc.)
- **Phase UI** — Browser dashboard for KB exploration
- **Phase REMOTE** — TLS + auth for accessing the local KB from a phone/laptop

## Disabling OmniNet (CI / Minimal Builds)

If you want to skip the OmniNet fetch (e.g., for CI without git access):

```bash
cmake -S . -B build -DOMNINET_SKIP_FETCH=ON
```

This requires OmniNet to be installed system-wide and findable via
`find_package(omninet)`. If the find fails, the build will abort.

## Version Pinning

OmniNet is pinned to a specific git tag (`0.7.1` as of this writing). To upgrade:

1. Check https://gitlab.com/leestripp/omninet/tags for the latest version
2. Update `GIT_TAG` in the top-level `CMakeLists.txt`
3. Reconfigure and rebuild
4. Test thoroughly — OmniNet is upstream, not under ApolloCore's control

## Troubleshooting

### "undefined reference to OmniNet::init"

You forgot to link against `OmniNet`:
```cmake
target_link_libraries(your_target PRIVATE OmniNet)
```

### "sqlite-vec not found" warnings

OmniNet builds sqlite-vec by default. ApolloCore doesn't use it. To silence
the warning, configure with:
```bash
cmake -S . -B build -DOMNINET_ENABLE_VEC=OFF
```

### First build is slow (~10 minutes)

That's OmniNet + llama.cpp + nlohmann/json + sqlite-vec all compiling from
source. Subsequent builds are incremental (seconds).

### `setHeader("Name", "value")` is ambiguous

OmniNet has two `setHeader` overloads. Be explicit:
```cpp
resp.setHeader(std::string_view("Name"), std::string_view("value"));
```

This is a known OmniNet API wart. Fixed in newer versions.

## More Info

- OmniNet repo: https://gitlab.com/leestripp/omninet
- ApolloCore + OmniNet integration plan: `CodeMax.md` → Phase O1
- Future REST API plan: `CodeMax.md` → Phase REST (TBD)
- MCP HTTP transport plan: `CodeMax.md` → Phase M1.16
