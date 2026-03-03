# Traffic Violation Detection – RZ/V2L Deployment

---

## Cấu trúc thư mục

```
exe_v2l/
├── traffic_violation_app          ← binary (cross-compile xong copy vào đây)
├── config.yaml                    ← chỉnh theo camera thực tế trước khi deploy
├── models/
│   └── traffic_yolov3/            ← single model – cần compile cho V2L
│       ├── deploy.json
│       ├── deploy.params
│       ├── deploy.so              ← compile từ .onnx với --devices V2L
│       ├── labels.txt             ← 7 dòng: motorbike, car, truck, bus, person, helmet, license_plate
│       └── preprocess/            ← 9 file DRP-AI preprocess (output compile)
└── violations/                    ← ảnh vi phạm (tự tạo khi chạy)
```

---

## Bước 1 – Train model

Model là **custom YOLOv3 7 class** – cần tự chuẩn bị dataset và train:

| Class ID | Nhãn |
|---|---|
| 0 | motorbike |
| 1 | car |
| 2 | truck |
| 3 | bus |
| 4 | person (rider) |
| 5 | helmet |
| 6 | license_plate |

Sau khi train xong, xuất ra file `.onnx` (ví dụ: `traffic_yolov3.onnx`).

---

## Bước 2 – Compile model cho V2L

Chạy trong môi trường DRP-AI TVM Docker:

```bash
cd /drp-ai_tvm/tutorials

python compile_onnx_model.py \
    --input_path  /path/to/traffic_yolov3.onnx \
    --output_path /drp-ai_tvm/data/traffic_violation/exe_v2l/models/traffic_yolov3 \
    --input_shape 1,3,416,416 \
    --devices V2L
```

Output cần có đủ:
```
models/traffic_yolov3/
├── deploy.json
├── deploy.params
├── deploy.so
└── preprocess/   (9 file)
```

**Tạo thêm `labels.txt`:**
```bash
printf "motorbike\ncar\ntruck\nbus\nperson\nhelmet\nlicense_plate\n" \
    > exe_v2l/models/traffic_yolov3/labels.txt
```

---

## Bước 3 – Cross-compile binary

Chạy trong **Renesas AI SDK Docker container** (biến `$SDK`, `$TVM_HOME` đã set sẵn):

```sh
cd /drp-ai_tvm/data/traffic_violation
mkdir -p build && cd build
cmake -DCMAKE_TOOLCHAIN_FILE=../toolchain/aarch64-cross.cmake ..
make -j$(nproc)
cp traffic_violation ../exe_v2l/traffic_violation_app
```

---

## Bước 4 – Chỉnh config.yaml

Mở `exe_v2l/config.yaml` và điều chỉnh theo thực tế:

| Mục | Ý nghĩa | Ghi chú |
|---|---|---|
| `video.source` | Đường dẫn video hoặc camera index | `/home/root/.../input.mp4` hoặc `0` |
| `video.width/height` | Độ phân giải capture | `640×480` cho performance tốt |
| `scene.stop_line.y1/y2` | Vị trí vạch dừng (0..1 theo chiều cao) | Đo từ ảnh camera |
| `scene.traffic_light_roi` | Vùng cắt đèn tín hiệu (x,y,w,h chuẩn hóa) | Đảm bảo bao phủ đèn |
| `scene.lane_zones` | Làn hợp lệ theo loại xe (x_min/x_max, 0..1) | Căn theo vạch trên đường |
| `scene.lane_lines` | Vị trí vạch phân làn tĩnh (`x_norm`) | Căn theo vạch thực tế |
| `detector.conf_threshold` | Ngưỡng tin cậy | Tăng nếu nhiều false positive |
| `detector.num_class` | Phải đúng với model đã train | Mặc định `7` |

**Thông số YOLO phải khớp với cấu hình lúc train:**
```yaml
detector:
  num_class:  7
  num_bb:     3
  num_layers: 3
  grids:      [13, 26, 52]
  anchors:    [10,13, 16,30, 33,23, 30,61, 62,45, 59,119, 116,90, 156,198, 373,326]
```

---

## Bước 5 – Copy lên kit

```bash
scp -r exe_v2l/ root@<board_ip>:/home/root/traffic_violation
```

---

## Bước 6 – Chạy trên kit

```bash
ssh root@<board_ip>
cd /home/root/traffic_violation
./traffic_violation_app config.yaml
```

Ảnh vi phạm lưu vào `violations/`:
```
51F12345_20260302_143022_015_NO_HELMET.jpg
29A99999_20260302_143105_123_RED_LIGHT.jpg
NOPLATE_20260302_143210_789_LANE_LINE_CROSS.jpg
```

---

## Các lỗi thường gặp

| Lỗi | Nguyên nhân | Cách sửa |
|---|---|---|
| `Input file is empty` (OpenCV crash) | `config.yaml` không tồn tại hoặc rỗng | Copy `exe_v2l/config.yaml` vào cùng thư mục với binary, hoặc truyền path: `./traffic_violation_app /full/path/config.yaml` |
| `config.yaml not found or empty` | Binary tìm config theo `exeDir()` nhưng file thiếu | Copy `config.yaml` vào `~/traffic_violation/` |
| `TrafficDetector::load() failed` | Thiếu `deploy.so` hoặc sai `model_dir` | Kiểm tra đường dẫn trong `config.yaml` |
| `Failed to load deploy.so` | Binary V2H dùng trên V2L | Compile lại cho V2L (Bước 2) |
| `Cannot open video source` | Sai đường dẫn hoặc camera index | Chỉnh `video.source` |
| `PreRuntime init failed` | Thiếu file trong `preprocess/` | Copy đủ output từ compile |
| Không detect được xe/helmet | Model chưa có deploy.so hoặc sai class ID | Compile model + kiểm tra `class_*` trong config |
| Không đọc được biển số | Tesseract không có trong rootfs | Cài `tesseract` vào Yocto image |

---

## Lưu ý

- **DRP-AI freq:** `drpai.freq_index: 2` = 1 GHz (khuyến nghị cho V2L)
- **Đè vạch (`LaneLineRule`):** Dùng vạch tĩnh từ `scene.lane_lines` – không cần segmentation.  
  Điều chỉnh `x_norm` chính xác theo góc nhìn camera.
- **Helmet detection:** Model phát hiện `class 5 = helmet` trong cùng frame với rider –  
  overlap ≥ 30% area mũ với bbox motorbike → coi là đội mũ.
- **LP OCR:** Chạy mỗi 10 frame để tiết kiệm CPU. Cần Tesseract 4.x trong rootfs.
- **1 DRP-AI core trên V2L:** Chỉ 1 model chạy tại 1 thời điểm – kiến trúc single model tận dụng tối đa.
