// SPDX-License-Identifier: MIT
//
// resources.hpp — MCP resources (read-only data the agent can subscribe to)
//
// Resources are different from tools: they're addressed by URI and the agent
// fetches them when it needs context (e.g. a list of projects), not because
// it wants to perform an action.

#pragma once

#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "server_state.hpp"

namespace apollo::mcp {

using nlohmann::json;

struct ResourceDef {
    std::string uri;             // e.g. "apollo://projects"
    std::string name;
    std::string title;           // human-readable
    std::string description;
    std::string mime_type = "application/json";
    json to_list_item() const;
};

struct ResourceReadResult {
    std::vector<std::pair<std::string, std::string>> contents;
    /// Each pair is {uri, text_or_blob}. We only emit text for now.

    json to_json() const;
};

using ResourceHandler = std::function<ResourceReadResult(const std::string& uri,
                                                         ServerState& state)>;

class ResourceRegistry {
public:
    void register_resource(ResourceDef def, ResourceHandler handler);

    json list() const;
    ResourceReadResult read(const std::string& uri, ServerState& state) const;

    /// Read the URI after stripping the `apollo://` scheme.
    std::string strip_scheme(const std::string& uri) const;

    size_t size() const { return resources_.size(); }

private:
    struct Entry { ResourceDef def; ResourceHandler handler; };
    std::vector<Entry> resources_;
};

ResourceRegistry build_default_resource_registry();

}  // namespace apollo::mcp