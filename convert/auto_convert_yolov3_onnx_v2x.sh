#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Auto-convert YOLOv3 .pt -> .onnx for RZ/V2L, RZ/V2M, RZ/V2MA flow.

Usage:
  auto_convert_yolov3_onnx_v2x.sh --pt <path/to/model.pt> [options]

Options:
  --pt PATH             Required. Path to YOLOv3 .pt file.
  --imgsz N             Input image size. Default: 640.
  --model-name NAME     Output model base name. Default: pt filename without .pt.
  --repo PATH           YOLOv3 repo path. Default: <TVM_ROOT>/convert/repos/ultralytics_yolov3
  --run-step5           Also run tutorials/compile_onnx_model.py after export.
  --keep-venv           Keep the created venv (default behavior).
  --cleanup-venv        Remove the created venv at the end.
  -h, --help            Show this help.

Environment:
  TVM_ROOT              Optional. If unset, inferred from this script location.
  PRODUCT               Needed only for --run-step5 (V2L, V2M, or V2MA).
  SDK                   Optional for step5. Path to SDK.
  TRANSLATOR            Optional for step5. Path to DRP-AI Translator.

Examples:
  # Convert + compile step5 for YOLOv3
  cd /drp-ai_tvm/convert
  PRODUCT=V2L SDK=/opt/poky/3.1.31 TRANSLATOR=/opt/drp-ai_translator_release \
  ./auto_convert_yolov3_onnx_v2x.sh --pt /duong_dan/model_yolov3.pt --run-step5
EOF
}

log() {
  printf '[INFO] %s\n' "$*"
}

warn() {
  printf '[WARN] %s\n' "$*" >&2
}

die() {
  printf '[ERROR] %s\n' "$*" >&2
  exit 1
}

require_cmd() {
  command -v "$1" >/dev/null 2>&1 || die "Command not found: $1"
}

PT_PATH=""
IMGSZ="640"
MODEL_NAME=""
REPO_PATH=""
RUN_STEP5="0"
CLEANUP_VENV="0"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --pt)
      PT_PATH="${2:-}"
      shift 2
      ;;
    --imgsz)
      IMGSZ="${2:-}"
      shift 2
      ;;
    --model-name)
      MODEL_NAME="${2:-}"
      shift 2
      ;;
    --repo)
      REPO_PATH="${2:-}"
      shift 2
      ;;
    --run-step5)
      RUN_STEP5="1"
      shift
      ;;
    --keep-venv)
      CLEANUP_VENV="0"
      shift
      ;;
    --cleanup-venv)
      CLEANUP_VENV="1"
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      die "Unknown option: $1"
      ;;
  esac
done

[[ -n "$PT_PATH" ]] || {
  usage
  die "--pt is required"
}

[[ "$IMGSZ" =~ ^[0-9]+$ ]] || die "--imgsz must be an integer"

require_cmd python3
require_cmd git

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TVM_ROOT="${TVM_ROOT:-$(cd "$SCRIPT_DIR/.." && pwd)}"
REPO_PATH="${REPO_PATH:-$TVM_ROOT/convert/repos/ultralytics_yolov3}"
PT_ABS="$(python3 -c 'import os,sys; print(os.path.abspath(sys.argv[1]))' "$PT_PATH")"

[[ -f "$PT_ABS" ]] || die "PT file not found: $PT_ABS"
[[ -d "$REPO_PATH" ]] || die "YOLOv3 repo path not found: $REPO_PATH"
[[ -f "$REPO_PATH/export.py" ]] || die "export.py not found in repo: $REPO_PATH"

pick_python_cmd() {
  local c
  for c in "${PYTHON_BIN:-}" python3.11 python3.10 python3.9 python3.8 python3; do
    if [[ -n "$c" ]] && command -v "$c" >/dev/null 2>&1; then
      local ver
      ver="$($c -c 'import sys; print(f"{sys.version_info.major}.{sys.version_info.minor}")')"
      local maj="${ver%%.*}"
      local min="${ver#*.}"
      if (( maj > 3 || (maj == 3 && min >= 8) )); then
        echo "$c"
        return 0
      fi
    fi
  done
  return 1
}

PYTHON_CMD="$(pick_python_cmd || true)"
[[ -n "$PYTHON_CMD" ]] || die "No Python >=3.8 found. Install python3.8+ or set PYTHON_BIN."

if [[ -z "$MODEL_NAME" ]]; then
  MODEL_NAME="$(basename "$PT_ABS")"
  MODEL_NAME="${MODEL_NAME%.pt}"
fi

DETECT_META_PY='import re, sys, zipfile
pt = sys.argv[1]
text = ""
if zipfile.is_zipfile(pt):
    with zipfile.ZipFile(pt) as zf:
        if "archive/data.pkl" in zf.namelist():
            text = zf.read("archive/data.pkl").decode("latin1", errors="ignore")
versions = []
if text:
    versions += re.findall(r"version.{0,80}?([0-9]+\.[0-9]+(?:\.[0-9]+)?)", text, re.IGNORECASE | re.DOTALL)
    versions += re.findall(r"v([0-9]+\.[0-9]+(?:\.[0-9]+)?)", text)
    versions += re.findall(r"\b([0-9]+\.[0-9]+\.[0-9]+)\b", text[-8000:])
# Deduplicate while preserving order.
out = []
for v in versions:
    if v not in out:
        out.append(v)
print(out[0] if out else "")'

CKPT_VER="$(python3 -c "$DETECT_META_PY" "$PT_ABS" | tr -d '\r\n')"

if [[ -z "$CKPT_VER" ]]; then
  warn "Could not detect checkpoint version from metadata."
  CKPT_VER="9.6.0"
  warn "Fallback checkpoint version: $CKPT_VER"
fi

PY_TAG="$($PYTHON_CMD -c 'import sys; print(f"py{sys.version_info.major}{sys.version_info.minor}")')"
VENV_DIR="$TVM_ROOT/convert/venvs/yolov3_onnx_${CKPT_VER}_${PY_TAG}"
OUTPUT_DIR="$TVM_ROOT/convert/output/${MODEL_NAME}_yolov3_onnx"
OUT_ONNX="$OUTPUT_DIR/${MODEL_NAME}.onnx"

log "TVM_ROOT       : $TVM_ROOT"
log "YOLOv3 repo    : $REPO_PATH"
log "PT model       : $PT_ABS"
log "Model name     : $MODEL_NAME"
log "Image size     : $IMGSZ"
log "Detected ver   : $CKPT_VER"
log "Python         : $PYTHON_CMD"
log "Venv path      : $VENV_DIR"
log "Output ONNX    : $OUT_ONNX"

if [[ -d "$VENV_DIR" ]]; then
  warn "Venv already exists, removing for clean run: $VENV_DIR"
  rm -rf "$VENV_DIR"
fi

"$PYTHON_CMD" -m venv "$VENV_DIR"
# shellcheck disable=SC1091
source "$VENV_DIR/bin/activate"

python -m pip install --upgrade pip setuptools wheel

# Version-aware torch selection for better compatibility.
VER_MAJOR="${CKPT_VER%%.*}"
if [[ "$VER_MAJOR" =~ ^[0-9]+$ ]] && (( VER_MAJOR >= 9 )); then
  TORCH_PIN=("torch==2.3.1+cpu" "torchvision==0.18.1+cpu")
elif [[ "$VER_MAJOR" =~ ^[0-9]+$ ]] && (( VER_MAJOR >= 7 )); then
  TORCH_PIN=("torch==2.2.2+cpu" "torchvision==0.17.2+cpu")
else
  TORCH_PIN=("torch==1.13.1+cpu" "torchvision==0.14.1+cpu")
fi

if ! pip install "${TORCH_PIN[@]}" -f https://download.pytorch.org/whl/torch_stable.html; then
  warn "Pinned torch install failed, trying CPU index fallback."
  pip install torch torchvision --index-url https://download.pytorch.org/whl/cpu
fi

if ! pip install -r "$REPO_PATH/requirements.txt"; then
  warn "Installing full requirements failed, installing minimal export deps."
  pip install matplotlib numpy opencv-python Pillow PyYAML requests scipy tqdm pandas seaborn thop
fi

if ! pip install onnx==1.16.0 onnxruntime==1.20.1; then
  warn "Pinned onnx/onnxruntime install failed, trying unpinned fallback."
  pip install onnx onnxruntime
fi

mkdir -p "$OUTPUT_DIR"

log "Exporting ONNX with YOLOv3 export.py..."
cd "$REPO_PATH"
python export.py --weights "$PT_ABS" --imgsz "$IMGSZ" "$IMGSZ" --include onnx --opset 12 --device cpu

PT_BASE="$(basename "$PT_ABS")"
CANDIDATE_ONNX_DIR="$(dirname "$PT_ABS")"
CANDIDATE_ONNX="$CANDIDATE_ONNX_DIR/${PT_BASE%.pt}.onnx"

if [[ -f "$CANDIDATE_ONNX" ]]; then
  mv -f "$CANDIDATE_ONNX" "$OUT_ONNX"
else
  LATEST_ONNX="$(ls -1t "$CANDIDATE_ONNX_DIR"/*.onnx 2>/dev/null | head -n 1 || true)"
  [[ -n "$LATEST_ONNX" ]] || die "No ONNX file found after export."
  mv -f "$LATEST_ONNX" "$OUT_ONNX"
fi

log "ONNX export complete: $OUT_ONNX"

COMPILE_INPUT="$($VENV_DIR/bin/python - <<PY
import onnx
m = onnx.load(r"$OUT_ONNX")
print(m.graph.input[0].name if m.graph.input else "images")
PY
)"

deactivate

COMPILE_CMD="python3 compile_onnx_model.py ../convert/output/${MODEL_NAME}_yolov3_onnx/${MODEL_NAME}.onnx -o ${MODEL_NAME}_onnx -s 1,3,${IMGSZ},${IMGSZ} -i ${COMPILE_INPUT}"

log "Step 5 compile command:"
printf '%s\n' "  cd $TVM_ROOT/tutorials"
printf '%s\n' "  $COMPILE_CMD"

if [[ "$RUN_STEP5" == "1" ]]; then
  [[ -n "${PRODUCT:-}" ]] || die "PRODUCT is not set. Example: export PRODUCT=V2L"
  case "${PRODUCT}" in
    V2L|V2M|V2MA) ;;
    *) die "Unsupported PRODUCT=${PRODUCT}. Use V2L, V2M, or V2MA" ;;
  esac

  if [[ -n "${SDK:-}" && ! -d "${SDK}" ]]; then
    die "SDK path does not exist: ${SDK}"
  fi
  if [[ -n "${TRANSLATOR:-}" && ! -d "${TRANSLATOR}" ]]; then
    die "TRANSLATOR path does not exist: ${TRANSLATOR}"
  fi

  log "Running step 5 compile script..."
  cd "$TVM_ROOT/tutorials"
  python3 compile_onnx_model.py "../convert/output/${MODEL_NAME}_yolov3_onnx/${MODEL_NAME}.onnx" -o "${MODEL_NAME}_onnx" -s "1,3,${IMGSZ},${IMGSZ}" -i "${COMPILE_INPUT}"
  log "Step 5 compile finished."
fi

if [[ "$CLEANUP_VENV" == "1" ]]; then
  rm -rf "$VENV_DIR"
  log "Removed venv: $VENV_DIR"
else
  log "Kept venv: $VENV_DIR"
fi

log "Done."
