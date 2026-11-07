# /// script
# dependencies = [
#   "mcp[cli]<2.0.0",
#   "llama-index-core",
#   "llama-index-readers-file",
#   "llama-index-embeddings-huggingface",
#   "torch",
# ]
# ///

import os
import sys
import logging
import warnings
import builtins
from contextlib import redirect_stdout

# 1. Environment & noise suppression
os.environ["HF_HUB_OFFLINE"] = "1"
os.environ["TRANSFORMERS_OFFLINE"] = "1"
os.environ["HF_HUB_DISABLE_IMPLICIT_TOKEN_WARNING"] = "1"
os.environ["HF_HUB_DISABLE_SYMLINKS_WARNING"] = "1"
os.environ["HF_HUB_DISABLE_PROGRESS_BARS"] = "1"
os.environ["TOKENIZERS_PARALLELISM"] = "false"

# Silence loggers
logging.disable(logging.CRITICAL)
warnings.simplefilter("ignore")

# Route print statements to stderr for stdout clean JSON-RPC
_original_print = builtins.print
def stderr_print(*args, **kwargs):
    if kwargs.get("file") is None or kwargs.get("file") == sys.stdout:
        kwargs["file"] = sys.stderr
    _original_print(*args, **kwargs)
builtins.print = stderr_print

from mcp.server.fastmcp import FastMCP

mcp = FastMCP("LlamaIndex Local CPU RAG")

# Pre-load heavy modules
with redirect_stdout(sys.stderr):
    import torch
    from llama_index.core import (
        VectorStoreIndex,
        SimpleDirectoryReader,
        StorageContext,
        load_index_from_storage,
        Settings,
    )
    from llama_index.embeddings.huggingface import HuggingFaceEmbedding
    from llama_index.core.llms import MockLLM

    # Large batch size for CPU throughput
    Settings.embed_model = HuggingFaceEmbedding(
        model_name="BAAI/bge-small-en-v1.5",
        device="cpu",
        embed_batch_size=64,
    )
    Settings.llm = MockLLM()


def load_retriever():
    """Loads existing index or builds a new one using all available CPU cores."""
    docs_dir = os.path.abspath(os.environ.get("DOCS_DIR", "./graphics_docs"))
    persist_dir = os.path.abspath(os.environ.get("PERSIST_DIR", "./.llama_storage_cpu"))
    docstore_path = os.path.join(persist_dir, "docstore.json")

    # 1. Fast-load existing index
    if os.path.exists(docstore_path):
        try:
            print(f"[MCP Info] Loading cached vector index from '{persist_dir}'...", file=sys.stderr)
            storage_context = StorageContext.from_defaults(persist_dir=persist_dir)
            index = load_index_from_storage(storage_context)
            print("[MCP Info] Index loaded successfully.", file=sys.stderr)
            return index.as_retriever(similarity_top_k=5)
        except Exception as e:
            print(f"[MCP Warning] Corrupted storage index ({e}). Rebuilding...", file=sys.stderr)

    # 2. Multi-threaded Build Phase
    if not os.path.exists(docs_dir):
        os.makedirs(docs_dir, exist_ok=True)
        print(f"[MCP Warning] Created empty folder '{docs_dir}'. Place files there.", file=sys.stderr)
        return None

    allowed_extensions = [".md", ".rst", ".txt", ".html", ".htm", ".h", ".hpp", ".cpp"]
    ignored_patterns = ["*/.git/*", "*/build/*", "*/tests/*", "*/test/*", "*/benchmarks/*", "*/cmake/*"]

    print(f"[MCP Info] Scanning documentation files in '{docs_dir}'...", file=sys.stderr)
    reader = SimpleDirectoryReader(
        input_dir=docs_dir,
        recursive=True,
        required_exts=allowed_extensions,
        exclude=ignored_patterns,
        exclude_hidden=True,
    )
    documents = reader.load_data()
    if not documents:
        print(f"[MCP Warning] No matching documentation files in '{docs_dir}'.", file=sys.stderr)
        return None

    cpu_cores = os.cpu_count() or 4
    print(f"[MCP Info] Generating embeddings for {len(documents)} sections across {cpu_cores} CPU cores...", file=sys.stderr)

    # Enable multi-core parallel CPU execution during indexing
    torch.set_num_threads(cpu_cores)
    try:
        os.makedirs(persist_dir, exist_ok=True)
        index = VectorStoreIndex.from_documents(documents)
        index.storage_context.persist(persist_dir=persist_dir)
        print(f"[MCP Info] Indexing complete. Persisted to '{persist_dir}'.", file=sys.stderr)
    finally:
        # Lock PyTorch back to 1 thread to avoid FastMCP worker deadlocks
        torch.set_num_threads(1)

    return index.as_retriever(similarity_top_k=5)


retriever = None

# Pre-load retriever during startup
with redirect_stdout(sys.stderr):
    try:
        retriever = load_retriever()
    except Exception as err:
        print(f"[MCP Warning] Startup index load deferred: {err}", file=sys.stderr)


@mcp.tool()
def search_docs(query: str) -> str:
    """Searches graphics engine documentation using a local CPU vector index."""
    global retriever
    try:
        with redirect_stdout(sys.stderr):
            # Ensure runtime query thread safety
            torch.set_num_threads(1)

            if retriever is None:
                retriever = load_retriever()

            if retriever is None:
                docs_dir = os.path.abspath(os.environ.get("DOCS_DIR", "./graphics_docs"))
                return f"Documentation index is empty or folder '{docs_dir}' is missing/empty. Add files and restart."

            nodes = retriever.retrieve(query)

        if not nodes:
            return "No matching documentation snippets found."

        results = []
        for n in nodes:
            score = round(n.score, 3) if n.score is not None else "N/A"
            results.append(f"### [Score: {score}]\n{n.node.get_content()}")

        return "\n\n---\n\n".join(results)
    except Exception as e:
        return f"Search error: {str(e)}"


if __name__ == "__main__":
    # Force single-threading before entering the stdio event loop
    torch.set_num_threads(1)

    # CLI option to force rebuild directly from terminal: python local_docs_mcp.py --build
    if len(sys.argv) > 1 and sys.argv[1] == "--build":
        persist_dir = os.path.abspath(os.environ.get("PERSIST_DIR", "./.llama_storage_cpu"))
        import shutil
        if os.path.exists(persist_dir):
            shutil.rmtree(persist_dir)
        load_retriever()
        sys.exit(0)

    mcp.run(transport="stdio")