# Traffic Violation Detection

Hệ thống phát hiện vi phạm giao thông tại ngã tư từ video, chạy trên **Renesas RZ/V2L** với **DRP-AI TVM**.

> **Kiến trúc hiện tại (tháng 3/2026):**  
> **1 model duy nhất** (custom YOLOv3, 7 class) thay thế hoàn toàn kiến trúc 4-model cũ.  
> Model cần tự train, sau đó compile bằng DRP-AI TVM cho V2L.

---

## Tính năng

| Vi phạm | Rule | Logic |
|---|---|---|
| Không đội mũ bảo hiểm | `HelmetRule` | Model detect class `helmet` (5) – kiểm tra overlap với bbox rider |
| Vượt đèn đỏ | `RedLightRule` | Xe qua stop-line khi HSV detect đèn đỏ trong ROI |
| Sai làn đường | `WrongLaneRule` | Centroid xe nằm ngoài `[x_min, x_max]` theo loại xe |
| Đè vạch kẻ đường | `LaneLineRule` | Bbox xe straddling qua vạch tĩnh ≥ `overlap_px` mỗi bên |

**Output:** `{BIEN_SO}_{YYYYMMDD_HHMMSS_mmm}_{LOAI_VI_PHAM}.jpg` + `.json`

---

## Kiến trúc hệ thống

```
Video (HD/4K)
    │
    ▼ VideoInput (GStreamer / OpenCV)
    │
    ▼ TrafficDetector ─────────────────── custom YOLOv3 416×416 (DRP-AI)
    │   7 class: motorbike(0) car(1) truck(2) bus(3)
    │             person(4)  helmet(5)  license_plate(6)
    │   Infer một lần / frame – trả về vector<detection>
    │
    ├─► VehicleTracker (CPU)
    │       Nhận toàn bộ dets, tự filter class vehicle/person
    │       IoU greedy matching, lưu trajectory 60 frame
    │
    ├─► Helmet association (CPU, inline main.cpp)
    │       Mỗi track MOTORBIKE: tìm det class helmet(5) có overlap > 30%
    │       tracker.setHelmet(id, found)
    │
    ├─► LP OCR mỗi 10 frame (CPU, inline main.cpp)
    │       Det class license_plate(6) overlap với bbox xe
    │       → Grayscale crop → TesseractEngine singleton
    │       → lp_trim_normalize → match_vn_plate → LpValidator
    │       → tracker.setPlate(id, "51F-12345")
    │
    ├─► detectTrafficLight (CPU ~0.1ms)
    │       HSV count pixel đỏ trong traffic_light_roi
    │
    ▼
ViolationEngine ──── confirm_frames=3 + cooldown per (track_id, type)
    │  HelmetRule, RedLightRule, WrongLaneRule, LaneLineRule
    ▼
ViolationLogger ──── JPEG + JSON → violations/
    [Wayland display khi -DWITH_DRP=ON]
```

### Ưu điểm so với kiến trúc 4-model

- **Latency thấp hơn:** 1 lần inference/frame thay vì 4
- **Không cần sync giữa model:** tất cả thông tin từ 1 inference
- **Đơn giản hơn:** 1 model file, 1 set tham số
- **Helmet detection chính xác hơn:** thấy toàn frame, không bị crop miss

---

## Cấu trúc dự án

```
traffic_violation/
├── CMakeLists.txt
├── config.yaml                    ← Dev/host config
├── toolchain/
│   └── aarch64-cross.cmake
├── exe_v2l/                       ← Thư mục deploy lên kit
│   ├── README.md
│   ├── config.yaml                ← Board config
│   └── models/
│       └── traffic_yolov3/        ← deploy.json/.params/.so + preprocess/ + labels.txt
└── src/
    ├── main.cpp                   ← Pipeline: infer → associate helmet/LP → violate
    ├── VideoInput.h/cpp
    ├── drp/                       ← Hardware (copy từ R01_object_detection)
    │   ├── define.h
    │   ├── wayland.h/cpp
    │   ├── dmabuf.h/cpp
    │   └── image.h/cpp
    ├── common/
    │   ├── types.h                ← Box, detection (R01 typedef), TrackedVehicle, ViolationType...
    │   ├── config.h/cpp           ← AppConfig, DetectorConfig, LaneLine, LaneZone
    │   ├── tess_module/           ← TesseractEngine singleton
    │   ├── lp_text_proc_module/   ← lp_trim_normalize()
    │   ├── lp_regex_module/       ← match_vn_plate(), create_lp_regex_list()
    │   └── lp_validate_module/    ← LpValidator
    ├── models/
    │   ├── box.h/cpp              ← NMS: filter_boxes_nms(), box_iou()
    │   └── TrafficDetector.h/cpp  ← load() + detect() – R01 YOLO decode pattern
    ├── tracking/
    │   └── VehicleTracker.h/cpp   ← IoU greedy tracker
    ├── violation/
    │   ├── IViolationRule.h       ← Interface plugin
    │   ├── ViolationEngine.h/cpp  ← confirm_frames + cooldown debounce
    │   ├── HelmetRule.h/cpp
    │   ├── RedLightRule.h/cpp
    │   ├── WrongLaneRule.h/cpp
    │   └── LaneLineRule.h/cpp     ← Đè vạch tĩnh từ config
    └── output/
        └── ViolationLogger.h/cpp
```

---

## Model

### Custom YOLOv3 – 7 classes

| Class ID | Tên | Ghi chú |
|---|---|---|
| 0 | motorbike | Xe máy |
| 1 | car | Ô tô con |
| 2 | truck | Xe tải |
| 3 | bus | Xe buýt |
| 4 | person | Người ngồi trên xe máy (rider) |
| 5 | helmet | Mũ bảo hiểm (worn on head) |
| 6 | license_plate | Biển số xe |

**Cần tự train** với dataset ngã tư Việt Nam. Sau khi có file `.onnx`:
```bash
python compile_onnx_model.py \
    --input_path  your_traffic_yolov3.onnx \
    --output_path exe_v2l/models/traffic_yolov3 \
    --input_shape 1,3,416,416 \
    --devices V2L
```

### Cấu trúc model folder

```
models/traffic_yolov3/
├── deploy.json
├── deploy.params
├── deploy.so              ← compile riêng cho V2L
├── labels.txt             ← 7 dòng: motorbike, car, truck, bus, person, helmet, license_plate
└── preprocess/            ← 9 file DRP-AI preprocess (từ output compile)
```

---

## Build

### Host x86_64 (không cần DRP-AI)

```bash
mkdir -p build && cd build
cmake .. -DWITH_DRP=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build . -- -j4
```

Yêu cầu: `libopencv-dev`, `libyaml-cpp-dev` (hoặc bundled), `libtesseract-dev`

### Cross-compile cho RZ/V2L

```bash
source /opt/poky/3.1.26/environment-setup-aarch64-poky-linux
cmake -B build_v2l \
      -DWITH_DRP=ON \
      -DCMAKE_TOOLCHAIN_FILE=toolchain/aarch64-cross.cmake \
      -DCMAKE_BUILD_TYPE=Release
cmake --build build_v2l -- -j4
cp build_v2l/traffic_violation exe_v2l/traffic_violation_app
```

---

## Cấu hình scene (config.yaml)

Mỗi camera/ngã tư cần căn chỉnh:

```yaml
scene:
  stop_line:
    y1: 0.60        # vị trí stop-line (tỉ lệ chiều cao, 0..1)
    y2: 0.63

  traffic_light_roi:
    x: 0.80         # góc trái trên (tỉ lệ 0..1)
    y: 0.05
    w: 0.08
    h: 0.20

  lane_zones:                               # làn hợp lệ cho từng loại xe
    motorbike: { x_min: 0.00, x_max: 0.50 }
    car:       { x_min: 0.25, x_max: 0.75 }
    truck:     { x_min: 0.25, x_max: 1.00 }

  lane_lines:                               # vạch phân làn tĩnh (đè vạch)
    - { x_norm: 0.33, overlap_px: 10 }     # vạch 1/3 trái
    - { x_norm: 0.66, overlap_px: 10 }     # vạch 2/3 phải
```

**`lane_lines`:** `x_norm` là vị trí vạch theo chiều ngang (0.0=trái, 1.0=phải).  
Xe vi phạm khi bbox chồng lên vạch ít nhất `overlap_px` pixel mỗi bên.

---

## OCR biển số

```
det class license_plate(6) → crop bbox → grayscale
    → resize nếu height < 32px
    → TesseractEngine::getInstance() (singleton, PSM_SINGLE_LINE)
    → GetUTF8Text()
    → lp_trim_normalize()     ← trim + xóa space + O→0 + toUpper
    → match_vn_plate()        ← regex: \d{2}[A-Z]{1,2}[-]?\d{4,5}
    → LpValidator::is_valid_plate() + format_plate()
    → "51F-12345"
```

Tesseract luôn link vào binary (không có flag `WITH_OCR`).  
Cần `libtesseract-dev` trên host và `tesseract` trong Yocto rootfs.

---

## Thêm loại vi phạm mới

1. Tạo `src/violation/MyRule.h/cpp` impl `IViolationRule`:
```cpp
class MyRule : public IViolationRule {
public:
    std::vector<ViolationEvent> check(const FrameContext& ctx) override;
};
```
2. Thêm field bool vào `ViolationConfig` (`config.h`) và parse trong `config.cpp`
3. Đăng ký trong `main.cpp`:
```cpp
if (cfg.violation.my_rule)
    engine.addRule(std::make_shared<MyRule>(...));
```
4. Thêm vào `ViolationType` enum và `violationName()` trong `types.h`

---

## Output ví dụ

```
violations/
├── 51F12345_20260315_143022_456_NO_HELMET.jpg
├── 51F12345_20260315_143022_456_NO_HELMET.json
├── 29A99999_20260315_143105_123_RED_LIGHT.jpg
├── UNKNOWN_T007_20260315_143210_789_WRONG_LANE.jpg
└── NOPLATE_20260315_143300_012_LANE_LINE_CROSS.jpg
```

---

## Deployment

Xem [exe_v2l/README.md](exe_v2l/README.md) để biết các bước chi tiết:  
train model → compile V2L → cross-compile binary → copy lên kit → chạy.
