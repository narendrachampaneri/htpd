#!/usr/bin/env bash
# Train and measure the HTPD models on your own PC (CPU only, no GPU).
#
#   ./scripts/train_decider.sh            everything (about 30-60 min on a normal PC)
#   ./scripts/train_decider.sh --quick    a short try with 2000 training pairs (a few minutes)
#
# What it does, in order:
#   1. installs the reader with the optional model libraries (sentence-transformers)
#      into .venv-models/ - your system Python is not touched
#   2. builds the converter if needed and gets the public evaluation manuals
#   3. makes the training data from the TRAINING manual only (model/data/)
#   4. fine-tunes the small open cross-encoder (22M parameters) on the CPU
#   5. tunes the "not found" threshold of every model variant on the tuning split
#   6. measures every variant on the test split and writes eval/report.md
#
# The models are downloaded once from Hugging Face (about 150 MB) and then
# work offline. Nothing is uploaded anywhere. A model is kept in HTPD only if
# eval/report.md shows that it beats "htpd-words" (no model) - see
# docs/PART-B.md rule 1.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"
limit=()
if [[ "${1:-}" == "--quick" ]]; then
  limit=(--limit 2000)
fi

step() { printf '\n== %s\n' "$*"; }

step "1/6 Python environment (.venv-models)"
python3 -c 'import sys; sys.exit(0 if sys.version_info >= (3, 10) else "Python 3.10 or newer is needed")'
if [[ ! -d .venv-models ]]; then
  python3 -m venv .venv-models
fi
# shellcheck disable=SC1091
source .venv-models/bin/activate
python -m pip install --quiet --upgrade pip
python -m pip install --quiet -e ".[models]"

step "2/6 Converter and evaluation manuals"
if [[ ! -x build/htpd ]]; then
  [[ -d third_party/pdfium ]] || ./scripts/get_pdfium.sh
  make
fi
eval/get_docs.sh

step "3/6 Training data (training manual only)"
python -m model.make_data eval/bundles/riscv-isa.htpd

step "4/6 Fine-tuning the cross-encoder on the CPU"
python -m model.train_cross_encoder "${limit[@]}"

systems=(htpd-words htpd htpd-ce htpd-ce-trained)
step "5/6 Tuning \"not found\" on the tuning split"
for s in "${systems[@]}"; do
  python -m eval tune "$s"
done

step "6/6 Measuring on the test split"
for s in B1 B3 "${systems[@]}"; do
  python -m eval run "$s"
done
python -m eval report > /dev/null

echo
echo "Done. The numbers are in eval/report.md:"
echo "  htpd-words       no model (what HTPD uses today)"
echo "  htpd             + meaning search (embedding model)"
echo "  htpd-ce          + cross-encoder, not trained on HTPD"
echo "  htpd-ce-trained  + cross-encoder trained here"
echo "A model is worth keeping only where it beats htpd-words."
