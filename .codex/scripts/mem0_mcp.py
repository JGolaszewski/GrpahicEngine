# /// script
# dependencies = [
#   "mcp[cli]<2.0.0",
#   "mem0ai",
#   "google-genai",
# ]
# ///

import os
import logging

# 1. Disable PostHog and Mem0 telemetry
os.environ["POSTHOG_DISABLED"] = "1"
os.environ["MEM0_TELEMETRY"] = "false"

# 2. Mute PostHog logging handlers
logging.getLogger("posthog").setLevel(logging.CRITICAL)

from mcp.server.fastmcp import FastMCP
from mem0 import Memory

mcp = FastMCP("Mem0 Gemini Engine")

config = {
    "llm": {
        "provider": "gemini",
        "config": {
            "model": "gemini-3.1-flash-lite",
            "api_key": os.environ.get("GEMINI_API_KEY"),
        },
    },
    "embedder": {
        "provider": "gemini",
        "config": {
            "model": "gemini-embedding-001",
            "api_key": os.environ.get("GEMINI_API_KEY"),
        },
    },
    "vector_store": {
        "provider": "qdrant",
        "config": {
            "host": "localhost",
            "port": 6333,
            "embedding_model_dims": 768,
        },
    },
}

_memory = None

def get_memory() -> Memory:
    global _memory
    if _memory is None:
        _memory = Memory.from_config(config)
    return _memory

@mcp.tool()
def add_memory(fact: str, user_id: str = "developer") -> str:
    """Adds a fact to the long-term memory."""
    mem = get_memory()
    mem.add(fact, user_id=user_id)
    return f"Remembered: {fact}"

@mcp.tool()
def search_memory(query: str, user_id: str = "developer") -> str:
    """Searches the long-term memory for information related to the query."""
    mem = get_memory()
    results = mem.search(query, filters={"user_id": user_id})

    if isinstance(results, dict):
        items = results.get("results", [])
    elif isinstance(results, list):
        items = results
    else:
        items = []

    memories = [m["memory"] for m in items if isinstance(m, dict) and "memory" in m]
    if not memories:
        return "No matching information found in memory."
    return "\n".join(f"- {m}" for m in memories)

if __name__ == "__main__":
    mcp.run()
