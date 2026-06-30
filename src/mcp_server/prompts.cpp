// SPDX-License-Identifier: MIT
//
// prompts.cpp — prompt definitions + handlers
//
// Pre-built workflows the user (or the agent acting on user's behalf) can
// invoke via `prompts/get`. Each one is a template that injects context from
// the KB into the conversation.

#include "prompts.hpp"

#include <stdexcept>

namespace apollo::mcp {

using nlohmann::json;

json PromptArgument::to_json() const
{
    return {
        {"name",        name},
        {"description", description},
        {"required",    required}
    };
}

json PromptDef::to_list_item() const
{
    json arr = json::array();
    for (const auto& a : arguments) arr.push_back(a.to_json());
    json item = {
        {"name",        name},
        {"description", description},
        {"arguments",   arr}
    };
    if (!title.empty()) item["title"] = title;
    return item;
}

json PromptMessage::to_json() const
{
    return {
        {"role",    role},
        {"content", {{"type", "text"}, {"text", content}}}
    };
}

json PromptGetResult::to_json() const
{
    json arr = json::array();
    for (const auto& m : messages) arr.push_back(m.to_json());
    json j = {{"messages", arr}};
    if (!description.empty()) j["description"] = description;
    return j;
}

void PromptRegistry::register_prompt(PromptDef def, PromptHandler handler)
{
    prompts_.push_back({std::move(def), std::move(handler)});
}

json PromptRegistry::list() const
{
    json arr = json::array();
    for (const auto& p : prompts_) arr.push_back(p.def.to_list_item());
    return {{"prompts", arr}};
}

PromptGetResult PromptRegistry::get(const std::string& name,
                                    const json& args,
                                    ServerState& state) const
{
    for (const auto& p : prompts_) {
        if (p.def.name == name) {
            try {
                return p.handler(args, state);
            } catch (const std::exception& e) {
                throw std::runtime_error(
                    std::string("Prompt '") + name + "' failed: " + e.what());
            }
        }
    }
    throw std::invalid_argument("Unknown prompt: " + name);
}

// ---------------------------------------------------------------------------
// Default prompts
// ---------------------------------------------------------------------------

static PromptGetResult prompt_summarize_kb(const json& /*args*/, ServerState& state)
{
    auto s = state.core().get_stats();
    PromptGetResult r;
    r.description = "Summarize the contents of the current knowledge base.";
    std::ostringstream o;
    o << "You are an assistant with access to an ApolloCore knowledge base.\n"
      << "Current stats:\n"
      << "  - documents:     " << s.document_count << "\n"
      << "  - chunks:        " << s.chunk_count << "\n"
      << "  - entities:      " << s.entity_count << "\n"
      << "  - relationships: " << s.relationship_count << "\n\n"
      << "Use the apollo_query_kb tool to investigate, and produce a concise "
      << "summary of the major topics, key entities, and any patterns you find.";
    r.messages.push_back({"user", o.str()});
    return r;
}

static PromptGetResult prompt_answer_with_citations(const json& args, ServerState& /*state*/)
{
    const std::string q = args.value("question", std::string{});
    PromptGetResult r;
    r.description = "Answer a question using only KB content, citing chunks.";
    std::ostringstream o;
    o << "Use the apollo_query_kb tool to answer this question:\n\n"
      << "  \"" << q << "\"\n\n"
      << "Constraints:\n"
      << "  - Only use information returned by the tool.\n"
      << "  - Cite the chunk_id and a 1-line excerpt for every claim.\n"
      << "  - If the KB does not contain the answer, say so explicitly.";
    r.messages.push_back({"user", o.str()});
    return r;
}

static PromptGetResult prompt_code_review_summary(const json& args, ServerState& /*state*/)
{
    const std::string path = args.value("path", std::string{"."});
    PromptGetResult r;
    r.description = "Walk through a codebase's structure as seen by ApolloCore.";
    std::ostringstream o;
    o << "Use apollo_ingest_codebase to ingest the codebase at: " << path << "\n"
      << "Then use apollo_search_symbols and apollo_get_neighbors to map out the "
      << "main types, functions, and the call graph between them. Produce a "
      << "human-readable overview: top-level modules, key entry points, the "
      << "3-5 most-called functions, and any suspicious patterns (e.g. deeply "
      << "nested call chains, wide-fanout functions, unowned singletons).";
    r.messages.push_back({"user", o.str()});
    return r;
}

static PromptGetResult prompt_entity_research(const json& args, ServerState& /*state*/)
{
    const std::string e = args.value("entity", std::string{});
    PromptGetResult r;
    r.description = "Compile everything we know about a single entity.";
    std::ostringstream o;
    o << "Use apollo_get_neighbors (hops=2) and apollo_query_kb to gather "
      << "every chunk that mentions or is linked to: \"" << e << "\"\n\n"
      << "Then produce a structured report: definition, related entities, "
      << "supporting evidence (chunk ids + excerpts), and any contradictions "
      << "you find in the KB.";
    r.messages.push_back({"user", o.str()});
    return r;
}

PromptRegistry build_default_prompt_registry()
{
    PromptRegistry r;

    r.register_prompt(
        {"summarize_kb", "Summarize KB",
         "Survey the current knowledge base and produce a concise overview.",
         {}},
        prompt_summarize_kb);

    r.register_prompt(
        {"answer_with_citations", "Answer with citations",
         "Answer a question using only KB content, with chunk-level citations.",
         {{"question", "The question to answer.", true}}},
        prompt_answer_with_citations);

    r.register_prompt(
        {"code_review_summary", "Code review summary",
         "Walk a codebase's structure from the KB's perspective.",
         {{"path", "Source directory to ingest first.", true}}},
        prompt_code_review_summary);

    r.register_prompt(
        {"entity_research", "Entity research",
         "Compile everything we know about a single entity.",
         {{"entity", "The entity name (e.g. a function, class, or person).", true}}},
        prompt_entity_research);

    return r;
}

}  // namespace apollo::mcp