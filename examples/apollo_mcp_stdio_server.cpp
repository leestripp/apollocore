/**
 * ApolloCore MCP stdio Server (Phase M2.14)
 *
 * JSON-RPC 2.0 over stdin/stdout with Content-Length framing.
 * Compatible with Claude Desktop, Continue.dev, and any MCP client
 * that spawns subprocesses.
 *
 * Usage:
 *   ./apollo_mcp_stdio_server --embedding-model models/embed.gguf \
 *                              [--llm-model models/qwen.gguf]
 *
 * Configuration for Claude Desktop (~/.config/claude_desktop_config.json):
 *   {
 *     "mcpServers": {
 *       "apollocore": {
 *         "command": "/path/to/apollo_mcp_stdio_server",
 *         "args": ["--embedding-model", "/path/to/embed.gguf"]
 *       }
 *     }
 *   }
 */

#include "apollocore/apollo_persistence.hpp"
#include "apollocore/apollo_mcp_stdio.hpp"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

namespace fs = std::filesystem;

struct Args {
    std::string embedding_model;
    std::string llm_model;
    std::string kb_path;          // Optional: open existing KB
    std::string kb_name = "stdio KB";
    std::string project_name;     // Optional: switch to this project on startup (M3.C)
};

Args parse_args(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&](const std::string& opt) -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "Missing value for %s\n", opt.c_str());
                std::exit(2);
            }
            return argv[++i];
        };
        if      (arg == "--embedding-model") a.embedding_model = next(arg);
        else if (arg == "--llm-model")       a.llm_model       = next(arg);
        else if (arg == "--kb")              a.kb_path         = next(arg);
        else if (arg == "--kb-name")         a.kb_name         = next(arg);
        else if (arg == "--project-name")    a.project_name    = next(arg);
        else if (arg == "-h" || arg == "--help") {
            std::fprintf(stderr,
                "ApolloCore MCP stdio Server (Phase M2.14)\n"
                "\n"
                "Usage: apollo_mcp_stdio_server [options]\n"
                "\n"
                "Options:\n"
                "  --embedding-model PATH  GGUF embedding model (required)\n"
                "  --llm-model PATH        GGUF LLM model for entity extraction\n"
                "  --kb PATH               Open existing KB on startup\n"
                "  --kb-name NAME          KB name if creating new\n"
                "  --project-name NAME     Switch to project NAME on startup (M3.C)\n"
                "                          Default: 'default'. Env: APOLLOCORE_PROJECT\n"
            );
            std::exit(0);
        } else {
            std::fprintf(stderr, "Unknown argument: %s\n", arg.c_str());
            std::exit(2);
        }
    }
    if (a.embedding_model.empty()) {
        std::fprintf(stderr, "Error: --embedding-model is required\n");
        std::exit(2);
    }
    return a;
}

int main(int argc, char** argv) {
    // All logging to stderr so stdout stays pure JSON-RPC
    Args args = parse_args(argc, argv);

    // Models
    std::fprintf(stderr, "[apollo_mcp_stdio] loading embedding model: %s\n",
                 args.embedding_model.c_str());
    auto kb = std::make_unique<apollocore_v2::ApolloCore>();
    if (!kb->load_embedding_model(args.embedding_model)) {
        std::fprintf(stderr, "[apollo_mcp_stdio] failed to load embedding model\n");
        return 1;
    }

    if (!args.llm_model.empty()) {
        std::fprintf(stderr, "[apollo_mcp_stdio] loading LLM model: %s\n",
                     args.llm_model.c_str());
        if (!kb->load_llm_model(args.llm_model)) {
            std::fprintf(stderr, "[apollo_mcp_stdio] failed to load LLM (entity extraction disabled)\n");
        }
    }

    // Optional KB
    if (!args.kb_path.empty()) {
        if (kb->open(args.kb_path)) {
            std::fprintf(stderr, "[apollo_mcp_stdio] opened KB: %s\n",
                         args.kb_path.c_str());
        } else {
            std::fprintf(stderr, "[apollo_mcp_stdio] failed to open KB: %s\n",
                         args.kb_path.c_str());
            return 1;
        }
    }

    // M3.C: Apply project context (--project-name or APOLLOCORE_PROJECT env)
    std::string project_name = args.project_name;
    if (project_name.empty()) {
        const char* env_p = std::getenv("APOLLOCORE_PROJECT");
        if (env_p) project_name = env_p;
    }
    if (!project_name.empty() && kb->is_open()) {
        // If project doesn't exist, auto-register it
        if (kb->get_project_info(project_name).is_null()) {
            kb->register_project(project_name, "Auto-created from CLI/env", false, "");
        }
        if (kb->set_current_project(project_name)) {
            std::fprintf(stderr, "[apollo_mcp_stdio] current project: %s\n",
                         project_name.c_str());
        }
    }

    std::fprintf(stderr, "[apollo_mcp_stdio] ready\n");

    // Run stdio server (blocks until EOF)
    int rc = apollocore_v2::mcp::run_stdio_server(*kb);

    std::fprintf(stderr, "[apollo_mcp_stdio] exited with code %d\n", rc);
    return rc;
}