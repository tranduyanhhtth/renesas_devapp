#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Auto-convert YOLOv8 .pt -> .onnx for RZ/V2L, RZ/V2M, RZ/V2MA flow.

Usage:
  auto_convert_yolov8_onnx_v2x.sh --pt <path/to/model.pt> [options]

Options:
  --pt PATH             Required. Path to YOLOv8 .pt file.
  --imgsz N             Input image size. If omitted, inferred from model name
                        (yolov8l/x => 320, others => 640).
  --model-name NAME     Output model base name. Default: pt filename without .pt.
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
  # Convert + compile step5 for YOLOv8
  cd /drp-ai_tvm/convert
  PRODUCT=V2L SDK=/opt/poky/3.1.31 TRANSLATOR=/opt/drp-ai_translator_release \
  ./auto_convert_yolov8_onnx_v2x.sh --pt /drp-ai_tvm/convert/yolov8n_bikehelmet.pt --run-step5
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
IMGSZ=""
MODEL_NAME=""
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

require_cmd python3
require_cmd git

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TVM_ROOT="${TVM_ROOT:-$(cd "$SCRIPT_DIR/.." && pwd)}"

PT_ABS="$(python3 -c 'import os,sys; print(os.path.abspath(sys.argv[1]))' "$PT_PATH")"
[[ -f "$PT_ABS" ]] || die "PT file not found: $PT_ABS"

pick_python_cmd() {
  local c
  for c in \
    "${PYTHON_BIN:-}" \
    "$TVM_ROOT/convert/venvs/ultralytics_custom_py39/bin/python" \
    python3.11 python3.10 python3.9 python3; do
    if [[ -n "$c" ]] && command -v "$c" >/dev/null 2>&1; then
      local ver
      ver="$($c -c 'import sys; print(f"{sys.version_info.major}.{sys.version_info.minor}")')"
      local maj="${ver%%.*}"
      local min="${ver#*.}"
      if (( maj > 3 || (maj == 3 && min >= 9) )); then
        echo "$c"
        return 0
      fi
    fi
  done
  return 1
}

PYTHON_CMD="$(pick_python_cmd || true)"
[[ -n "$PYTHON_CMD" ]] || die "No Python >=3.9 found. Install python3.9+ (with venv) or set PYTHON_BIN to a compatible interpreter."

if [[ -z "$MODEL_NAME" ]]; then
  MODEL_NAME="$(basename "$PT_ABS")"
  MODEL_NAME="${MODEL_NAME%.pt}"
fi

if [[ -z "$IMGSZ" ]]; then
  case "${MODEL_NAME,,}" in
    *yolov8l*|*yolov8x*) IMGSZ="320" ;;
    *) IMGSZ="640" ;;
  esac
fi

[[ "$IMGSZ" =~ ^[0-9]+$ ]] || die "--imgsz must be an integer"

DETECT_VERSION_PY='import os, re, sys, zipfile
pt = sys.argv[1]
if not zipfile.is_zipfile(pt):
    print("")
    raise SystemExit(0)
with zipfile.ZipFile(pt) as zf:
    if "archive/data.pkl" not in zf.namelist():
        print("")
        raise SystemExit(0)
    data = zf.read("archive/data.pkl")
text = data.decode("latin1", errors="ignore")
# Typical Ultralytics checkpoint stores ... version ... 8.x.y ...
m = re.search(r"version.{0,60}?([0-9]+\.[0-9]+\.[0-9]+)", text, re.IGNORECASE | re.DOTALL)
if not m:
    # Fallback: pick first 8.x.y near the end where metadata resides
    tail = text[-5000:]
    m = re.search(r"(8\.[0-9]+\.[0-9]+)", tail)
print(m.group(1) if m else "")'

ULTRA_VER="$(python3 -c "$DETECT_VERSION_PY" "$PT_ABS" | tr -d '\r\n')"
if [[ -z "$ULTRA_VER" ]]; then
  warn "Could not detect ultralytics version from checkpoint metadata."
  ULTRA_VER="8.0.104"
  warn "Falling back to ultralytics==$ULTRA_VER"
fi

PY_TAG="$($PYTHON_CMD -c 'import sys; print(f"py{sys.version_info.major}{sys.version_info.minor}")')"
VENV_DIR="$TVM_ROOT/convert/venvs/ultralytics_onnx_${ULTRA_VER}_${PY_TAG}"
OUTPUT_DIR="$TVM_ROOT/convert/output/${MODEL_NAME}_ultralytics_onnx"
OUT_ONNX="$OUTPUT_DIR/${MODEL_NAME}.onnx"

log "TVM_ROOT       : $TVM_ROOT"
log "PT model       : $PT_ABS"
log "Model name     : $MODEL_NAME"
log "Image size     : $IMGSZ"
log "Ultralytics    : $ULTRA_VER (auto-detected)"
log "Python         : $PYTHON_CMD"
log "Venv path      : $VENV_DIR"
log "Output ONNX    : $OUT_ONNX"

if [[ -d "$VENV_DIR" ]]; then
  warn "Venv already exists, removing for a clean run: $VENV_DIR"
  rm -rf "$VENV_DIR"
fi

"$PYTHON_CMD" -m venv "$VENV_DIR"
# shellcheck disable=SC1091
source "$VENV_DIR/bin/activate"

python -m pip install --upgrade pip setuptools wheel

if ! pip install "torch==2.3.1+cpu" "torchvision==0.18.1+cpu" -f https://download.pytorch.org/whl/torch_stable.html; then
  warn "Pinned torch install failed, trying CPU index fallback."
  pip install torch torchvision --index-url https://download.pytorch.org/whl/cpu
fi

if ! pip install onnx==1.16.0 onnxruntime==1.20.1; then
  warn "Pinned onnx/onnxruntime install failed, trying unpinned fallback."
  pip install onnx onnxruntime
fi

# Some exported checkpoints require numpy._core (numpy >= 1.26.1).
if ! pip install "numpy>=1.26.1,<2.0"; then
  warn "Could not pin numpy>=1.26.1,<2.0. Keeping currently resolved numpy."
fi

if ! pip install "ultralytics==$ULTRA_VER"; then
  warn "ultralytics==$ULTRA_VER install failed, trying a compatible minor range."
  ULTRA_MAJOR="${ULTRA_VER%%.*}"
  ULTRA_REST="${ULTRA_VER#*.}"
  ULTRA_MINOR="${ULTRA_REST%%.*}"
  NEXT_MINOR=$((ULTRA_MINOR + 1))
  pip install "ultralytics>=${ULTRA_MAJOR}.${ULTRA_MINOR},<${ULTRA_MAJOR}.${NEXT_MINOR}"
fi

mkdir -p "$OUTPUT_DIR"

WORK_DIR="$(dirname "$PT_ABS")"
PT_BASE="$(basename "$PT_ABS")"
cd "$WORK_DIR"

log "Exporting ONNX..."
yolo mode=export model="$PT_BASE" format=onnx opset=12 imgsz="$IMGSZ"

CANDIDATE_ONNX="${PT_BASE%.pt}.onnx"
if [[ -f "$WORK_DIR/$CANDIDATE_ONNX" ]]; then
  mv -f "$WORK_DIR/$CANDIDATE_ONNX" "$OUT_ONNX"
else
  # Fallback to latest generated ONNX in the model directory.
  LATEST_ONNX="$(ls -1t "$WORK_DIR"/*.onnx 2>/dev/null | head -n 1 || true)"
  [[ -n "$LATEST_ONNX" ]] || die "No ONNX file found after export."
  mv -f "$LATEST_ONNX" "$OUT_ONNX"
fi

log "ONNX export complete: $OUT_ONNX"

COMPILE_INPUT="$($VENV_DIR/bin/python - <<PY
import onnx
model = onnx.load(r"$OUT_ONNX")
print(model.graph.input[0].name if model.graph.input else "data")
PY
)"

deactivate

COMPILE_CMD="python3 compile_onnx_model.py ../convert/output/${MODEL_NAME}_ultralytics_onnx/${MODEL_NAME}.onnx -o ${MODEL_NAME}_onnx -s 1,3,${IMGSZ},${IMGSZ} -i ${COMPILE_INPUT}"

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
  python3 compile_onnx_model.py "../convert/output/${MODEL_NAME}_ultralytics_onnx/${MODEL_NAME}.onnx" -o "${MODEL_NAME}_onnx" -s "1,3,${IMGSZ},${IMGSZ}" -i "${COMPILE_INPUT}"
  log "Step 5 compile finished."
fi

if [[ "$CLEANUP_VENV" == "1" ]]; then
  rm -rf "$VENV_DIR"
  log "Removed venv: $VENV_DIR"
else
  log "Kept venv: $VENV_DIR"
fi

log "Done."
