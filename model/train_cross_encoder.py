"""Phase 5: fine-tune an open cross-encoder on the phase 4 data. Runs on a CPU.

    python3 -m pip install sentence-transformers
    python -m model.make_data eval/bundles/riscv-isa.htpd       # phase 4 data
    python -m model.train_cross_encoder                         # about 20-60 minutes on a laptop CPU

The model learns the "relevant?" and "enough?" decisions as one yes/no task:
(question, card or text) -> answers or not. It starts from
cross-encoder/ms-marco-MiniLM-L-6-v2 (22M parameters, Apache-2.0) and is saved
to model/checkpoints/cross-encoder-htpd. Then:

    python -m eval tune htpd-ce-trained
    python -m eval run  htpd-ce-trained
"""

from __future__ import annotations

import argparse
import json
import math
import random
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "model" / "data"
OUT = ROOT / "model" / "checkpoints" / "cross-encoder-htpd"
BASE = "cross-encoder/ms-marco-MiniLM-L-6-v2"


def pairs(path: Path) -> list[tuple[str, str, float]]:
    out = []
    for line in path.read_text(encoding="utf-8").splitlines():
        e = json.loads(line)
        if e["task"] == "relevant":
            out.append((e["question"], e["card"], float(e["label"])))
        elif e["task"] == "enough":
            out.append((e["question"], e["text"], float(e["label"])))
    return out


def auc(scores: list[float], labels: list[float]) -> float:
    pos = [s for s, y in zip(scores, labels) if y > 0.5]
    neg = [s for s, y in zip(scores, labels) if y <= 0.5]
    if not pos or not neg:
        return float("nan")
    wins = sum((p > n) + 0.5 * (p == n) for p in pos for n in neg)
    return wins / (len(pos) * len(neg))


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(prog="python -m model.train_cross_encoder")
    ap.add_argument("--data", default=str(DATA))
    ap.add_argument("--base", default=BASE)
    ap.add_argument("--out", default=str(OUT))
    ap.add_argument("--epochs", type=int, default=1)
    ap.add_argument("--batch", type=int, default=16)
    ap.add_argument("--limit", type=int, default=0, help="use only this many training pairs (a quick try)")
    args = ap.parse_args(argv)
    try:
        from sentence_transformers import CrossEncoder, InputExample  # type: ignore
        from torch.utils.data import DataLoader  # type: ignore
    except ImportError:
        print("needs: python3 -m pip install sentence-transformers", file=sys.stderr)
        return 2
    train = pairs(Path(args.data) / "train.jsonl")
    dev = pairs(Path(args.data) / "dev.jsonl")
    random.Random(0).shuffle(train)
    if args.limit:
        train = train[: args.limit]
    print(f"{len(train)} training pairs, {len(dev)} dev pairs, base {args.base}")
    model = CrossEncoder(args.base, num_labels=1, max_length=384, device="cpu")
    before = model.predict([(q, t) for q, t, _ in dev], show_progress_bar=False).tolist() if dev else []
    loader = DataLoader([InputExample(texts=[q, t], label=y) for q, t, y in train], shuffle=True,
                        batch_size=args.batch)
    model.fit(train_dataloader=loader, epochs=args.epochs, warmup_steps=math.ceil(len(loader) * 0.1),
              show_progress_bar=True)
    model.save(args.out)
    if dev:
        after = model.predict([(q, t) for q, t, _ in dev], show_progress_bar=False).tolist()
        labels = [y for _, _, y in dev]
        print(f"dev AUC: before {auc(before, labels):.3f}, after {auc(after, labels):.3f}")
    print(f"saved to {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
