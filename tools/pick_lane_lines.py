"""
Traffic Camera Annotation Tool
Vẽ vùng giám sát (vạch đường, đèn giao thông, vùng vi phạm) lên ảnh camera
Lưu tọa độ vào config.yaml (normalized [0..1]) để dùng trong xử lý vi phạm
"""

import tkinter as tk
from tkinter import ttk, filedialog, messagebox
import yaml
import os
from PIL import Image, ImageTk

# ── Constants ──────────────────────────────────────────────────────────────
# rectangle: click-drag; line: 2-point click
SHAPE_TYPES = ["line", "rectangle"]

LABEL_PRESETS = [
    "stop_line",        # vạch dừng xe        → lưu dạng line (y1/y2)
    "lane_line",        # vạch phân làn        → lưu dạng line (x_norm)
    "traffic_light",    # vùng đèn giao thông  → lưu dạng rect (x,y,w,h)
    "lane",             # làn đường xe         → lưu dạng rect (x_min/x_max)
    "no_entry_zone",    # vùng cấm vào         → lưu dạng rect
    "custom",
]

COLORS = {
    "stop_line":        "#FF4444",
    "lane_line":        "#FFB800",
    "traffic_light":    "#00FF88",
    "lane":             "#4499FF",
    "no_entry_zone":    "#FF44FF",
    "custom":           "#FFFFFF",
}

# Default draw mode per label
LABEL_DEFAULT_MODE = {
    "stop_line":     "line",
    "lane_line":     "line",
    "traffic_light": "rectangle",
    "lane":          "rectangle",
    "no_entry_zone": "rectangle",
    "custom":        "rectangle",
}

POINT_RADIUS = 3
CONFIG_PATH  = "renesas_devapp/config.yaml"


# ── Main App ───────────────────────────────────────────────────────────────
class TrafficAnnotator:
    def __init__(self, root):
        self.root = root
        self.root.title("Traffic Camera Annotator")
        self.root.configure(bg="#1a1a2e")
        self.root.geometry("1300x800")

        # Image state
        self.image_path = None
        self.pil_image  = None
        self.tk_image   = None
        self.scale      = 1.0
        self.offset_x   = 0
        self.offset_y   = 0

        # Shape store
        self.shapes         = []   # list of dicts
        self.selected_shape = None

        # Line drawing state
        self.line_p1        = None   # first click for line mode
        self.line_p1_cid    = None   # canvas id of p1 dot

        # Rectangle drawing state (drag)
        self.rect_drag_start = None  # (img_x, img_y) on mouse-down
        self.rect_preview_id = None  # canvas id of preview rect

        # UI vars
        self.draw_mode      = tk.StringVar(value="rectangle")
        self.label_var      = tk.StringVar(value="stop_line")
        self.custom_label   = tk.StringVar(value="")
        self.lane_type_var  = tk.StringVar(value="motorbike")  # for "lane" label

        self._build_ui()

    # ── UI ─────────────────────────────────────────────────────────────────
    def _build_ui(self):
        left = tk.Frame(self.root, bg="#16213e", width=230)
        left.pack(side=tk.LEFT, fill=tk.Y, padx=(8, 0), pady=8)
        left.pack_propagate(False)

        self._panel_title(left, "⚙ TOOLS")
        self._btn(left, "Load Image", self.load_image, "#0f3460")

        ttk.Separator(left, orient="horizontal").pack(fill=tk.X, pady=6)
        self._panel_title(left, "DRAW MODE")
        for mode in SHAPE_TYPES:
            tk.Radiobutton(
                left, text=mode.upper(), variable=self.draw_mode, value=mode,
                bg="#16213e", fg="#e0e0e0", selectcolor="#0f3460",
                activebackground="#16213e", activeforeground="#fff",
                font=("Courier", 10, "bold")
            ).pack(anchor=tk.W, padx=16, pady=2)

        ttk.Separator(left, orient="horizontal").pack(fill=tk.X, pady=6)
        self._panel_title(left, "LABEL")
        for label in LABEL_PRESETS:
            color = COLORS.get(label, "#fff")
            tk.Radiobutton(
                left, text=label, variable=self.label_var, value=label,
                command=self._on_label_change,
                bg="#16213e", fg=color, selectcolor="#0f3460",
                activebackground="#16213e", activeforeground=color,
                font=("Courier", 9)
            ).pack(anchor=tk.W, padx=16, pady=1)

        tk.Label(left, text="Custom label:", bg="#16213e", fg="#aaa",
                 font=("Courier", 9)).pack(anchor=tk.W, padx=16, pady=(6, 0))
        tk.Entry(left, textvariable=self.custom_label, bg="#0f3460",
                 fg="#fff", insertbackground="#fff",
                 font=("Courier", 9)).pack(fill=tk.X, padx=16, pady=2)

        # Lane-type sub-option (shown when label == "lane")
        self.lane_frame = tk.Frame(left, bg="#16213e")
        tk.Label(self.lane_frame, text="Lane type:", bg="#16213e", fg="#aaa",
                 font=("Courier", 9)).pack(anchor=tk.W, padx=16)
        for lt in ("motorbike", "car", "truck"):
            tk.Radiobutton(
                self.lane_frame, text=lt, variable=self.lane_type_var, value=lt,
                bg="#16213e", fg=COLORS["lane"], selectcolor="#0f3460",
                activebackground="#16213e", font=("Courier", 8)
            ).pack(anchor=tk.W, padx=24)

        ttk.Separator(left, orient="horizontal").pack(fill=tk.X, pady=6)
        self._panel_title(left, "🖱 ACTIONS")
        self._btn(left, "Cancel  [Esc]", self.cancel_current, "#4a3800")
        self._btn(left, "Delete",       self.delete_selected, "#5a1a1a")

        ttk.Separator(left, orient="horizontal").pack(fill=tk.X, pady=6)
        self._btn(left, "Save config", self.save_config, "#1a3a5c")
        self._btn(left, "Clear All",         self.clear_all,   "#3a1a1a")

        self.status_var = tk.StringVar(value="Load an image to start")
        tk.Label(left, textvariable=self.status_var, bg="#16213e", fg="#88aacc",
                 font=("Courier", 8), wraplength=210, justify=tk.LEFT
                 ).pack(side=tk.BOTTOM, padx=8, pady=8)

        # Right side: canvas + shape list
        right = tk.Frame(self.root, bg="#1a1a2e")
        right.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=8, pady=8)

        self.canvas = tk.Canvas(right, bg="#0d0d1a", cursor="crosshair",
                                highlightthickness=0)
        self.canvas.pack(fill=tk.BOTH, expand=True)

        list_frame = tk.Frame(right, bg="#16213e", height=160)
        list_frame.pack(fill=tk.X, pady=(4, 0))
        list_frame.pack_propagate(False)

        tk.Label(list_frame, text="SHAPES", bg="#16213e", fg="#88aacc",
                 font=("Courier", 9, "bold")).pack(anchor=tk.W, padx=8, pady=2)

        cols = ("id", "type", "label", "coords (normalized)")
        self.tree = ttk.Treeview(list_frame, columns=cols, show="headings", height=5)
        for c, w in zip(cols, (40, 90, 160, 500)):
            self.tree.heading(c, text=c.upper())
            self.tree.column(c, width=w, anchor=tk.W)

        style = ttk.Style()
        style.theme_use("clam")
        style.configure("Treeview", background="#0f1a30", foreground="#e0e0e0",
                        fieldbackground="#0f1a30", rowheight=20, font=("Courier", 8))
        style.configure("Treeview.Heading", background="#16213e", foreground="#88aacc")

        sb = ttk.Scrollbar(list_frame, orient=tk.VERTICAL, command=self.tree.yview)
        self.tree.configure(yscrollcommand=sb.set)
        sb.pack(side=tk.RIGHT, fill=tk.Y)
        self.tree.pack(fill=tk.BOTH, expand=True, padx=4)
        self.tree.bind("<<TreeviewSelect>>", self._on_tree_select)

        # Canvas bindings
        self.canvas.bind("<Button-1>",        self._on_click)
        self.canvas.bind("<B1-Motion>",       self._on_drag)
        self.canvas.bind("<ButtonRelease-1>", self._on_release)
        self.canvas.bind("<Motion>",          self._on_motion)
        self.canvas.bind("<Configure>",       self._on_resize)
        self.root.bind("<Escape>",            lambda e: self.cancel_current())

    def _panel_title(self, parent, text):
        tk.Label(parent, text=text, bg="#16213e", fg="#88aacc",
                 font=("Courier", 9, "bold")).pack(anchor=tk.W, padx=8, pady=(6, 2))

    def _btn(self, parent, text, cmd, color="#0f3460"):
        tk.Button(parent, text=text, command=cmd, bg=color, fg="#e0e0e0",
                  activebackground="#1a4a8a", activeforeground="#fff",
                  relief=tk.FLAT, bd=0, font=("Courier", 9),
                  padx=8, pady=4, anchor=tk.W
                  ).pack(fill=tk.X, padx=8, pady=2)

    def _on_label_change(self):
        label = self.label_var.get()
        # Auto-switch draw mode
        mode = LABEL_DEFAULT_MODE.get(label, "rectangle")
        self.draw_mode.set(mode)
        # Show/hide lane sub-option
        if label == "lane":
            self.lane_frame.pack(fill=tk.X, padx=0, pady=2)
        else:
            self.lane_frame.pack_forget()
        self.cancel_current()

    # ── Image loading ───────────────────────────────────────────────────────
    def load_image(self):
        path = filedialog.askopenfilename(
            filetypes=[("Images", "*.jpg *.jpeg *.png *.bmp *.tiff"), ("All", "*.*")])
        if not path:
            return
        self.clear_all(confirm=False)
        self.image_path = path
        self.pil_image  = Image.open(path)
        self._fit_image()
        self.status(f"Loaded: {os.path.basename(path)}")

    def _fit_image(self):
        if not self.pil_image:
            return
        cw = self.canvas.winfo_width()  or 900
        ch = self.canvas.winfo_height() or 600
        iw, ih = self.pil_image.size
        scale = min(cw / iw, ch / ih, 1.0)
        self.scale    = scale
        nw, nh        = int(iw * scale), int(ih * scale)
        self.offset_x = (cw - nw) // 2
        self.offset_y = (ch - nh) // 2
        resized       = self.pil_image.resize((nw, nh), Image.LANCZOS)
        self.tk_image = ImageTk.PhotoImage(resized)
        self._redraw()

    def _on_resize(self, event):
        self._fit_image()

    # ── Coord helpers ───────────────────────────────────────────────────────
    def _c2i(self, cx, cy):
        """Canvas → image pixel."""
        return ((cx - self.offset_x) / self.scale,
                (cy - self.offset_y) / self.scale)

    def _i2c(self, ix, iy):
        """Image pixel → canvas."""
        return (ix * self.scale + self.offset_x,
                iy * self.scale + self.offset_y)

    def _normalize(self, ix, iy):
        """Image pixel → normalized [0..1]."""
        if not self.pil_image:
            return ix, iy
        return (ix / self.pil_image.width, iy / self.pil_image.height)

    # ── Mouse events ────────────────────────────────────────────────────────
    def _on_click(self, event):
        if not self.pil_image:
            self.status("⚠ Load an image first!"); return

        mode = self.draw_mode.get()

        if mode == "line":
            img_pt = self._c2i(event.x, event.y)
            if self.line_p1 is None:
                # First point
                self.line_p1 = img_pt
                r = POINT_RADIUS
                self.line_p1_cid = self.canvas.create_oval(
                    event.x-r, event.y-r, event.x+r, event.y+r,
                    fill=self._cur_color(), outline="#fff", width=1, tags="current")
                self.status("Line: click second point  |  [Esc] cancel")
            # second point handled in _on_release for consistency

        elif mode == "rectangle":
            # Start drag
            self.rect_drag_start = self._c2i(event.x, event.y)

    def _on_drag(self, event):
        if self.draw_mode.get() != "rectangle" or self.rect_drag_start is None:
            return
        self.canvas.delete("rect_preview")
        sx, sy = self._i2c(*self.rect_drag_start)
        color  = self._cur_color()
        self.canvas.create_rectangle(
            sx, sy, event.x, event.y,
            outline=color, fill="", width=2, dash=(4, 4), tags="rect_preview")

    def _on_release(self, event):
        mode = self.draw_mode.get()

        if mode == "line" and self.line_p1 is not None:
            p2 = self._c2i(event.x, event.y)
            # Only register if mouse moved a bit (avoid accidental same-point)
            dx = abs(p2[0] - self.line_p1[0])
            dy = abs(p2[1] - self.line_p1[1])
            if dx + dy < 3:
                return
            self._finish_line(p2)

        elif mode == "rectangle" and self.rect_drag_start is not None:
            end_pt = self._c2i(event.x, event.y)
            self.canvas.delete("rect_preview")
            x1, y1 = self.rect_drag_start
            x2, y2 = end_pt
            if abs(x2 - x1) < 3 and abs(y2 - y1) < 3:
                self.rect_drag_start = None
                return
            self._finish_rect(x1, y1, x2, y2)
            self.rect_drag_start = None

    def _on_motion(self, event):
        if self.draw_mode.get() == "line" and self.line_p1 is not None:
            self.canvas.delete("line_preview")
            sx, sy = self._i2c(*self.line_p1)
            color  = self._cur_color()
            self.canvas.create_line(
                sx, sy, event.x, event.y,
                fill=color, dash=(4, 4), width=2, tags="line_preview")

    # ── Shape creation ──────────────────────────────────────────────────────
    def _finish_line(self, p2):
        label = self._effective_label()
        color = self._cur_color()
        shape = {
            "id":         len(self.shapes),
            "type":       "line",
            "label":      label,
            "color":      color,
            "points":     [self.line_p1, p2],
        }
        self.shapes.append(shape)
        # Reset line state
        self.line_p1     = None
        if self.line_p1_cid:
            self.canvas.delete(self.line_p1_cid)
            self.line_p1_cid = None
        self.canvas.delete("line_preview")
        self._redraw()
        self._update_tree()
        self.status(f"✅ Line saved: {label}")

    def _finish_rect(self, x1, y1, x2, y2):
        # Normalize so top-left < bottom-right
        rx1, ry1 = min(x1, x2), min(y1, y2)
        rx2, ry2 = max(x1, x2), max(y1, y2)
        label = self._effective_label()
        # For "lane", embed lane type into label
        if self.label_var.get() == "lane":
            label = f"lane_{self.lane_type_var.get()}"
        color = self._cur_color()
        shape = {
            "id":     len(self.shapes),
            "type":   "rectangle",
            "label":  label,
            "color":  color,
            "points": [(rx1, ry1), (rx2, ry2)],  # top-left, bottom-right (pixels)
        }
        self.shapes.append(shape)
        self._redraw()
        self._update_tree()
        self.status(f"✅ Rect saved: {label}")

    def _effective_label(self):
        label = self.label_var.get()
        if label == "custom":
            return self.custom_label.get().strip() or "custom"
        return label

    def _cur_color(self):
        return COLORS.get(self.label_var.get(), "#ffffff")

    # ── Cancel / delete ─────────────────────────────────────────────────────
    def cancel_current(self):
        self.line_p1 = None
        if self.line_p1_cid:
            self.canvas.delete(self.line_p1_cid)
            self.line_p1_cid = None
        self.canvas.delete("line_preview")
        self.rect_drag_start = None
        self.canvas.delete("rect_preview")
        self.status("Cancelled")

    def delete_selected(self):
        if self.selected_shape is None:
            self.status("No shape selected"); return
        self.shapes = [s for s in self.shapes if s["id"] != self.selected_shape]
        self.selected_shape = None
        self._redraw()
        self._update_tree()
        self.status("Shape deleted")

    def clear_all(self, confirm=True):
        if confirm and self.shapes:
            if not messagebox.askyesno("Clear All", "Delete all shapes?"):
                return
        self.shapes = []
        self.cancel_current()
        self._redraw()
        self._update_tree()
        self.status("Cleared")

    # ── Redraw ──────────────────────────────────────────────────────────────
    def _redraw(self):
        self.canvas.delete("all")
        if self.tk_image:
            self.canvas.create_image(self.offset_x, self.offset_y,
                                     anchor=tk.NW, image=self.tk_image)
        for shape in self.shapes:
            self._draw_shape(shape)

    def _draw_shape(self, shape):
        color      = shape.get("color", "#fff")
        is_sel     = (shape["id"] == self.selected_shape)
        outline    = "#FFD700" if is_sel else color
        width      = 3 if is_sel else 2
        pts        = shape["points"]
        canvas_pts = [self._i2c(x, y) for x, y in pts]

        if shape["type"] == "line":
            flat = [c for pt in canvas_pts for c in pt]
            self.canvas.create_line(*flat, fill=outline, width=width,
                                    arrow=tk.LAST, arrowshape=(10, 12, 4))
            r = POINT_RADIUS
            for cx, cy in canvas_pts:
                self.canvas.create_oval(cx-r, cy-r, cx+r, cy+r,
                                        fill=color, outline="#fff", width=1)
        else:  # rectangle
            (x1, y1), (x2, y2) = canvas_pts
            self.canvas.create_rectangle(
                x1, y1, x2, y2, outline=outline, fill="", width=width,
                dash=() if is_sel else (6, 2))

        # Label text
        lx = sum(p[0] for p in canvas_pts) / len(canvas_pts)
        ly = min(p[1] for p in canvas_pts) - 14
        self.canvas.create_rectangle(
            lx - 4, ly - 10, lx + len(shape["label"]) * 7 + 4, ly + 4,
            fill="#111111", outline="")
        self.canvas.create_text(
            lx, ly - 3, text=shape["label"], fill=color,
            font=("Courier", 8, "bold"), anchor=tk.W)

    # ── Tree list ───────────────────────────────────────────────────────────
    def _update_tree(self):
        for item in self.tree.get_children():
            self.tree.delete(item)
        iw = self.pil_image.width  if self.pil_image else 1
        ih = self.pil_image.height if self.pil_image else 1
        for s in self.shapes:
            pts = s["points"]
            if s["type"] == "line":
                coords = "  →  ".join(
                    f"({x/iw:.3f}, {y/ih:.3f})" for x, y in pts)
            else:
                (x1, y1), (x2, y2) = pts
                coords = (f"x:[{x1/iw:.3f}–{x2/iw:.3f}]  "
                          f"y:[{y1/ih:.3f}–{y2/ih:.3f}]")
            self.tree.insert("", tk.END,
                             values=(s["id"], s["type"], s["label"], coords))

    def _on_tree_select(self, event):
        sel = self.tree.selection()
        if not sel:
            return
        vals = self.tree.item(sel[0], "values")
        if vals:
            self.selected_shape = int(vals[0])
            self._redraw()

    # ── Save YAML ───────────────────────────────────────────────────────────
    def _norm_pt(self, ix, iy):
        """Return (nx, ny) normalized to [0..1]."""
        iw = self.pil_image.width
        ih = self.pil_image.height

        nx = max(0, min(ix / iw, 1))
        ny = max(0, min(iy / ih, 1))
        return round(nx, 4), round(ny, 4)

    def save_config(self):
        if not self.shapes:
            self.status("Nothing to save"); return
        if not self.pil_image:
            self.status("No image loaded"); return

        scene = {}

        # ── stop_line  (horizontal line → y1/y2) ──────────────────────────
        stop_lines = [s for s in self.shapes if s["label"] == "stop_line"]
        if stop_lines:
            s   = stop_lines[-1]          # keep last drawn
            pts = s["points"]
            all_y = [self._norm_pt(*p)[1] for p in pts]
            y_mid = sum(all_y) / len(all_y)
            # For a horizontal line both y values are similar; use min/max
            scene["stop_line"] = {
                "y1": round(y_mid - 0.01, 4),
                "y2": round(y_mid + 0.01, 4)
            }

        # ── lane_zones  (rectangle per vehicle type) ──────────────────────
        lane_shapes = [s for s in self.shapes if s["label"].startswith("lane_")]
        if lane_shapes:
            lane_zones = {}
            for s in lane_shapes:
                vtype = s["label"][len("lane_"):]   # "motorbike" / "car" / "truck"
                (x1, y1), (x2, y2) = s["points"]
                nx1, _ = self._norm_pt(x1, y1)
                nx2, _ = self._norm_pt(x2, y2)
                lane_zones[vtype] = {
                    "x_min": min(nx1, nx2),
                    "x_max": max(nx1, nx2),
                }
            scene["lane_zones"] = lane_zones

        # ── lane_lines  (vertical lines → x_norm) ─────────────────────────
        lane_line_shapes = [s for s in self.shapes if s["label"] == "lane_line"]
        if lane_line_shapes:
            lane_lines = []
            for s in lane_line_shapes:
                xs = [self._norm_pt(*p)[0] for p in s["points"]]
                x_norm = round(sum(xs) / len(xs), 4)
                lane_lines.append({"x_norm": x_norm, "overlap_px": 10})
            scene["lane_lines"] = lane_lines

        # ── traffic_light_roi  (rectangle → x,y,w,h) ─────────────────────
        tl_shapes = [s for s in self.shapes if s["label"] == "traffic_light"]
        if tl_shapes:
            s           = tl_shapes[-1]
            (x1, y1), (x2, y2) = s["points"]
            nx1, ny1    = self._norm_pt(x1, y1)
            nx2, ny2    = self._norm_pt(x2, y2)
            scene["traffic_light_roi"] = {
                "x": min(nx1, nx2),
                "y": min(ny1, ny2),
                "w": round(abs(nx2 - nx1), 4),
                "h": round(abs(ny2 - ny1), 4),
            }

        # ── no_entry_zone  (rectangle, arbitrary) ─────────────────────────
        no_entry = [s for s in self.shapes if s["label"] == "no_entry_zone"]
        if no_entry:
            zones = []
            for s in no_entry:
                (x1, y1), (x2, y2) = s["points"]
                nx1, ny1 = self._norm_pt(x1, y1)
                nx2, ny2 = self._norm_pt(x2, y2)
                zones.append({
                    "x_min": min(nx1, nx2), "x_max": max(nx1, nx2),
                    "y_min": min(ny1, ny2), "y_max": max(ny1, ny2),
                })
            scene["no_entry_zones"] = zones

        # ── custom shapes ─────────────────────────────────────────────────
        custom_shapes = [s for s in self.shapes
                         if s["label"] not in (
                             "stop_line", "lane_line", "traffic_light",
                             "no_entry_zone") and not s["label"].startswith("lane_")]
        if custom_shapes:
            customs = []
            for s in custom_shapes:
                entry = {"label": s["label"], "type": s["type"], "points": []}
                for p in s["points"]:
                    nx, ny = self._norm_pt(*p)
                    entry["points"].append([nx, ny])
                customs.append(entry)
            scene["custom"] = customs

        # ── Assemble full config ──────────────────────────────────────────
        config = {
            "# Camera / scene ROI (normalized [0..1])": None,
            "scene": scene,
        }

        # Ensure output directory exists
        os.makedirs(os.path.dirname(CONFIG_PATH), exist_ok=True)

        # Load existing config (preserve all other keys)
        existing = {}
        if os.path.exists(CONFIG_PATH):
            with open(CONFIG_PATH, "r", encoding="utf-8") as f:
                existing = yaml.safe_load(f) or {}

        # Merge: only update keys present in new scene, keep the rest
        if "scene" not in existing:
            existing["scene"] = {}
        existing["scene"].update(scene)

        with open(CONFIG_PATH, "w", encoding="utf-8") as f:
            yaml.dump(existing, f, default_flow_style=False,
                      allow_unicode=True, sort_keys=False)

        self.status(f"💾 Saved: {CONFIG_PATH}")

    # ── Status ──────────────────────────────────────────────────────────────
    def status(self, msg):
        self.status_var.set(msg)


# ── Entry point ────────────────────────────────────────────────────────────
if __name__ == "__main__":
    root = tk.Tk()
    app  = TrafficAnnotator(root)
    root.mainloop()