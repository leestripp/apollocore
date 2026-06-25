/**
 * ApolloCore REST API (Phase R1) - Header
 *
 * Public declarations for the REST API route handlers.
 * Implementation is in apollo_rest_api.cpp.
 */

#pragma once

#include "OmniNet.h"

#include <memory>

namespace apollocore_v2 {

// Forward declaration (defined in apollo_persistence.hpp)
class ApolloCore;

namespace rest {

/**
 * The active ApolloCore instance for the REST server.
 * Set once at startup by apollo_rest_server; accessed by all handlers.
 * Single KB per server (matches ApolloCore design).
 */
extern std::unique_ptr<ApolloCore> g_kb;

/**
 * Register all REST API routes on the given OmniNet HTTP server.
 *
 * @param server The OmniNet server to register routes on
 *
 * Routes registered:
 *   GET    /api/health
 *   POST   /api/kb/create
 *   POST   /api/kb/open
 *   POST   /api/kb/close
 *   DELETE /api/kb
 *   GET    /api/kb/stats
 *   POST   /api/documents
 *   POST   /api/search
 *   GET    /api/entities
 *   GET    /api/relationships
 *   POST   /api/extract
 */
void register_routes(OmniNet::HTTP::Server& server);

} // namespace rest
} // namespace apollocore_v2