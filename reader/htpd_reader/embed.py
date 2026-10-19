"""Meaning search: turn texts into vectors with a small open embedding model.

The model runs on the CPU and is downloaded once (then it works offline).
The default is `BAAI/bge-small-en-v1.5` (MIT licence, 33M parameters) through
the `sentence-transformers` library. Neither is needed for the rest of
htpd_reader: without them, meaning search is simply not available.

Nothing is written into a bundle. The vectors of a bundle can be cached in a
folder of the caller's choice, keyed by the model and the texts, so a bundle
is encoded only once.
"""

from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path
from typing import Protocol, Sequence

DEFAULT_MODEL = "BAAI/bge-small-en-v1.5"
# bge models want this in front of a question (not in front of passages)
QUERY_PREFIX = {"BAAI/bge-small-en-v1.5": "Represent this sentence for searching relevant passages: "}


class Embedder(Protocol):
    name: str

    def encode(self, texts: Sequence[str], query: bool = False) -> list[list[float]]: ...


class NotAvailable(RuntimeError):
    """The embedding library or model is not installed."""


class SentenceTransformerEmbedder:
    def __init__(self, model: str = DEFAULT_MODEL, device: str = "cpu", batch_size: int = 32):
        try:
            from sentence_transformers import SentenceTransformer  # type: ignore
        except ImportError as e:  # pragma: no cover - depends on the machine
            raise NotAvailable(
                "meaning search needs: python3 -m pip install sentence-transformers  "
                f"(then the model {model} is downloaded once)") from e
        self.name = model
        self._model = SentenceTransformer(model, device=device)
        self._batch = batch_size
        self._prefix = QUERY_PREFIX.get(model, "")

    def encode(self, texts: Sequence[str], query: bool = False) -> list[list[float]]:
        if query and self._prefix:
            texts = [self._prefix + t for t in texts]
        vecs = self._model.encode(list(texts), batch_size=self._batch, normalize_embeddings=True,
                                  show_progress_bar=False)
        return [list(map(float, v)) for v in vecs]


def normalize(v: list[float]) -> list[float]:
    n = math.sqrt(sum(x * x for x in v)) or 1.0
    return [x / n for x in v]


def cosine_top(query: list[float], vectors: list[list[float]], k: int) -> list[tuple[int, float]]:
    """(index, cosine) of the k nearest vectors. Vectors must be normalised."""
    try:
        import numpy as np  # type: ignore
    except ImportError:
        scores = [sum(a * b for a, b in zip(query, v)) for v in vectors]
    else:
        if not vectors:
            return []
        scores = (np.asarray(vectors, dtype="float32") @ np.asarray(query, dtype="float32")).tolist()
    order = sorted(range(len(scores)), key=lambda i: (-scores[i], i))[:k]
    return [(i, float(scores[i])) for i in order]


def cached_encode(embedder: Embedder, texts: list[str], cache_dir: Path | None, key: str) -> list[list[float]]:
    """Encode passages once; later runs read them from cache_dir."""
    if cache_dir is None:
        return embedder.encode(texts)
    digest = hashlib.sha256("\x00".join(texts).encode("utf-8")).hexdigest()[:16]
    safe = "".join(c if c.isalnum() or c in "-._" else "_" for c in f"{key}-{embedder.name}-{digest}")
    path = cache_dir / f"{safe}.json"
    if path.is_file():
        return json.loads(path.read_text(encoding="utf-8"))
    vecs = embedder.encode(texts)
    cache_dir.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(vecs), encoding="utf-8")
    return vecs
