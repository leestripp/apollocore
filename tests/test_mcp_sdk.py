"""Phase E1 — adopt the official `mcp` Python SDK as a client.

This test file uses the SDK's `ClientSession` against ApolloCore's
`/mcp` endpoint (Streamable HTTP transport). The point of using the
SDK — instead of raw urllib — is to verify that a *real* MCP client
can handshake, list, and call our server without us custom-coding the
JSON-RPC framing.

If this passes, we know ApolloCore is wired correctly enough to be
discovered + consumed by Claude Desktop, the MCP Inspector, and any
SDK-based agent.

Run with the venv where `mcp` is installed:
    /tmp/venv_test/bin/pytest -xvs tests/test_mcp_sdk.py

Or skip this file in the regular ctest run (it's gated by importing
`mcp`, which may not be available in CI's system Python).
"""
import json
import os
import sys
import pytest
import asyncio

# Make the import optional — the test suite doesn't depend on mcp.
mcp = pytest.importorskip("mcp")

from mcp import ClientSession
from mcp.client.streamable_http import streamablehttp_client

HOST = os.environ.get("APOLLO_HOST", "127.0.0.1")
PORT = int(os.environ.get("APOLLO_PORT", "8765"))
URL = f"http://{HOST}:{PORT}/mcp"


def _is_alive() -> bool:
    import urllib.request
    try:
        with urllib.request.urlopen(f"http://{HOST}:{PORT}/health", timeout=2) as r:
            return r.status == 200
    except Exception:
        return False


pytestmark = pytest.mark.skipif(
    not _is_alive(),
    reason="apollo_mcp_server not running on localhost:8765")


@pytest.mark.asyncio
async def test_sdk_handshake():
    """The SDK can connect and complete the initialize handshake."""
    async with streamablehttp_client(URL) as (read, write, _):
        async with ClientSession(read, write) as session:
            init = await session.initialize()
            assert init.serverInfo.name == "apollo_mcp_server"
            assert init.protocolVersion == "2025-11-25"
            # Capabilities advertised
            caps = init.capabilities
            assert caps.tools is not None
            assert caps.resources is not None
            assert caps.prompts is not None
            print(f"  SDK handshake OK: serverInfo={init.serverInfo.name} "
                  f"protocol={init.protocolVersion} "
                  f"tools={caps.tools}")


@pytest.mark.asyncio
async def test_sdk_list_tools():
    async with streamablehttp_client(URL) as (read, write, _):
        async with ClientSession(read, write) as session:
            await session.initialize()
            result = await session.list_tools()
            names = {t.name for t in result.tools}
            # We ship 9 tools (4 KB-lifecycle tools removed per prompt.txt).
            assert len(names) == 9, \
                f"expected 9 tools, got {len(names)}: {names}"
            expected = {
                "apollo_query_kb", "apollo_add_document",
                "apollo_extract_entities", "apollo_ingest_codebase",
                "apollo_search_symbols", "apollo_get_neighbors",
                "apollo_get_stats",
                "apollo_list_projects", "apollo_set_config",
            }
            assert names == expected, \
                f"tool set mismatch: missing={expected-names} extra={names-expected}"
            # All tools have a description (spec-required)
            for t in result.tools:
                assert t.description, f"{t.name} missing description"
                assert t.inputSchema, f"{t.name} missing inputSchema"
            print(f"  SDK list_tools: {len(names)} tools, all have schema ✓")


@pytest.mark.asyncio
async def test_sdk_call_simple_tool():
    """Call apollo_get_stats via the SDK — confirms tools/call round-trip."""
    async with streamablehttp_client(URL) as (read, write, _):
        async with ClientSession(read, write) as session:
            await session.initialize()
            result = await session.call_tool("apollo_get_stats", {})
            # The result is a CallToolResult with content[] and is_error.
            assert not result.isError, \
                f"apollo_get_stats returned isError: {result.content}"
            # We emit a text content block + structuredContent.
            assert len(result.content) >= 1, "no content blocks returned"
            text = result.content[0].text
            stats = json.loads(text)
            for key in ("documents", "chunks", "entities", "relationships"):
                assert key in stats, f"missing {key} in stats: {stats}"
                assert isinstance(stats[key], int), \
                    f"{key} should be int, got {type(stats[key])}"
            # And structuredContent, if present, matches.
            if hasattr(result, "structuredContent") and \
                    result.structuredContent:
                sc = result.structuredContent
                for key in ("documents", "chunks", "entities", "relationships"):
                    assert sc[key] == stats[key], \
                        f"structuredContent mismatch on {key}"
            print(f"  SDK call_tool(apollo_get_stats) -> {stats} ✓")


@pytest.mark.asyncio
async def test_sdk_list_resources():
    async with streamablehttp_client(URL) as (read, write, _):
        async with ClientSession(read, write) as session:
            await session.initialize()
            result = await session.list_resources()
            assert len(result.resources) >= 5, \
                f"expected ≥5 resources, got {len(result.resources)}"
            print(f"  SDK list_resources: {len(result.resources)} resources ✓")


@pytest.mark.asyncio
async def test_sdk_list_prompts():
    async with streamablehttp_client(URL) as (read, write, _):
        async with ClientSession(read, write) as session:
            await session.initialize()
            result = await session.list_prompts()
            assert len(result.prompts) >= 4, \
                f"expected ≥4 prompts, got {len(result.prompts)}"
            print(f"  SDK list_prompts: {len(result.prompts)} prompts ✓")


@pytest.mark.asyncio
async def test_sdk_read_resource():
    """resources/read on apollo://stats returns the same JSON as
    apollo_get_stats tool."""
    async with streamablehttp_client(URL) as (read, write, _):
        async with ClientSession(read, write) as session:
            await session.initialize()
            result = await session.read_resource("apollo://stats")
            assert result.contents, "empty resource contents"
            text = result.contents[0].text
            stats = json.loads(text)
            for key in ("documents", "chunks", "entities", "relationships"):
                assert key in stats, f"missing {key}"
            print(f"  SDK read_resource(apollo://stats) -> {stats} ✓")


@pytest.mark.asyncio
async def test_sdk_call_tool_with_args():
    """Call apollo_list_projects — simplest argument-free tool."""
    async with streamablehttp_client(URL) as (read, write, _):
        async with ClientSession(read, write) as session:
            await session.initialize()
            result = await session.call_tool("apollo_list_projects", {})
            assert not result.isError
            text = result.content[0].text
            proj_list = json.loads(text)
            assert isinstance(proj_list, list)
            print(f"  SDK call_tool(apollo_list_projects) -> "
                  f"{len(proj_list)} project(s) ✓")