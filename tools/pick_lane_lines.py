"""
Traffic Camera Annotation Tool
Vẽ vùng giám sát (vạch đường, đèn giao thông, vùng vi phạm) lên ảnh camera
Lưu tọa độ vào JSON để dùng trong xử lý vi phạm
"""

import tkinter as tk
from tkinter import ttk, filedialog, messagebox, simpledialog
import json
import os
from PIL import Image, ImageTk

# ── Constants ──────────────────────────────────────────────────────────────
SHAPE_TYPES = ["line", "polygon"]
LABEL_PRESETS = [
    "stop_line",        # vạch dừng xe
    "lane_line",        # vạch đi bộ
    "traffic_light",    # vùng đèn giao thông
    "lane",             # làn đường
    "no_entry_zone",    # vùng cấm vào
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
POINT_RADIUS = 3


# ── Main App ───────────────────────────────────────────────────────────────
class TrafficAnnotator:
    def __init__(self, root):
        self.root = root
        self.root.title("🚦 Traffic Camera Annotator")
        self.root.configure(bg="#1a1a2e")
        self.root.geometry("1300x800")

        # State
        self.image_path = None
        self.pil_image = None
        self.tk_image = None
        self.scale = 1.0
        self.offset_x = 0
        self.offset_y = 0

        self.shapes = []          # list of dicts: {type, label, points, color, canvas_ids}
        self.current_points = []  # points being drawn
        self.current_canvas_ids = []
        self.selected_shape = None

        self.draw_mode = tk.StringVar(value="polygon")
        self.label_var = tk.StringVar(value="stop_line")
        self.custom_label_var = tk.StringVar(value="")

        self._build_ui()

    # ── UI ─────────────────────────────────────────────────────────────────
    def _build_ui(self):
        # Left panel
        left = tk.Frame(self.root, bg="#16213e", width=220)
        left.pack(side=tk.LEFT, fill=tk.Y, padx=(8, 0), pady=8)
        left.pack_propagate(False)

        self._panel_title(left, "⚙ TOOLS")

        # Load image
        self._btn(left, "📂 Load Image", self.load_image, "#0f3460")

        ttk.Separator(left, orient="horizontal").pack(fill=tk.X, pady=8)
        self._panel_title(left, "✏ DRAW MODE")

        for mode in SHAPE_TYPES:
            rb = tk.Radiobutton(left, text=mode.upper(), variable=self.draw_mode,
                                value=mode, bg="#16213e", fg="#e0e0e0",
                                selectcolor="#0f3460", activebackground="#16213e",
                                activeforeground="#fff", font=("Courier", 10, "bold"))
            rb.pack(anchor=tk.W, padx=16, pady=2)

        ttk.Separator(left, orient="horizontal").pack(fill=tk.X, pady=8)
        self._panel_title(left, "🏷 LABEL")

        for label in LABEL_PRESETS:
            color = COLORS.get(label, "#fff")
            rb = tk.Radiobutton(left, text=label, variable=self.label_var,
                                value=label, bg="#16213e", fg=color,
                                selectcolor="#0f3460", activebackground="#16213e",
                                activeforeground=color, font=("Courier", 9))
            rb.pack(anchor=tk.W, padx=16, pady=1)

        tk.Label(left, text="Custom label:", bg="#16213e", fg="#aaa",
                 font=("Courier", 9)).pack(anchor=tk.W, padx=16, pady=(6, 0))
        tk.Entry(left, textvariable=self.custom_label_var, bg="#0f3460",
                 fg="#fff", insertbackground="#fff",
                 font=("Courier", 9)).pack(fill=tk.X, padx=16, pady=2)

        ttk.Separator(left, orient="horizontal").pack(fill=tk.X, pady=8)
        self._panel_title(left, "🖱 ACTIONS")

        self._btn(left, "✅ Finish Shape  [Enter]", self.finish_shape, "#155724")
        self._btn(left, "↩ Undo Point    [Z]",     self.undo_point,   "#4a3800")
        self._btn(left, "❌ Cancel Shape  [Esc]",   self.cancel_shape, "#5a1a1a")
        self._btn(left, "🗑 Delete Selected",        self.delete_selected, "#5a1a1a")

        ttk.Separator(left, orient="horizontal").pack(fill=tk.X, pady=8)
        self._btn(left, "💾 Save JSON",  self.save_json,  "#1a3a5c")
        self._btn(left, "📥 Load JSON",  self.load_json,  "#1a3a1a")
        self._btn(left, "🗑 Clear All",  self.clear_all,  "#3a1a1a")

        # Status bar at bottom of left panel
        self.status_var = tk.StringVar(value="Load an image to start")
        tk.Label(left, textvariable=self.status_var, bg="#16213e", fg="#88aacc",
                 font=("Courier", 8), wraplength=200, justify=tk.LEFT
                 ).pack(side=tk.BOTTOM, padx=8, pady=8)

        # Right: canvas + shape list
        right = tk.Frame(self.root, bg="#1a1a2e")
        right.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=8, pady=8)

        # Canvas
        self.canvas = tk.Canvas(right, bg="#0d0d1a", cursor="crosshair",
                                highlightthickness=0)
        self.canvas.pack(fill=tk.BOTH, expand=True)

        # Shape list
        list_frame = tk.Frame(right, bg="#16213e", height=160)
        list_frame.pack(fill=tk.X, pady=(4, 0))
        list_frame.pack_propagate(False)

        tk.Label(list_frame, text="SHAPES", bg="#16213e", fg="#88aacc",
                 font=("Courier", 9, "bold")).pack(anchor=tk.W, padx=8, pady=2)

        cols = ("id", "type", "label", "points")
        self.tree = ttk.Treeview(list_frame, columns=cols, show="headings", height=5)
        for c, w in zip(cols, (40, 80, 160, 400)):
            self.tree.heading(c, text=c.upper())
            self.tree.column(c, width=w, anchor=tk.W)

        style = ttk.Style()
        style.theme_use("clam")
        style.configure("Treeview", background="#0f1a30", foreground="#e0e0e0",
                        fieldbackground="#0f1a30", rowheight=20,
                        font=("Courier", 8))
        style.configure("Treeview.Heading", background="#16213e", foreground="#88aacc")

        sb = ttk.Scrollbar(list_frame, orient=tk.VERTICAL, command=self.tree.yview)
        self.tree.configure(yscrollcommand=sb.set)
        sb.pack(side=tk.RIGHT, fill=tk.Y)
        self.tree.pack(fill=tk.BOTH, expand=True, padx=4)
        self.tree.bind("<<TreeviewSelect>>", self._on_tree_select)

        # Bindings
        self.canvas.bind("<Button-1>",   self._on_click)
        self.canvas.bind("<Motion>",     self._on_motion)
        self.canvas.bind("<Configure>",  self._on_resize)
        self.root.bind("<Return>",       lambda e: self.finish_shape())
        self.root.bind("<Escape>",       lambda e: self.cancel_shape())
        self.root.bind("z",              lambda e: self.undo_point())
        self.root.bind("Z",              lambda e: self.undo_point())

        # Rubber-band line
        self.rubber_line = None

    def _panel_title(self, parent, text):
        tk.Label(parent, text=text, bg="#16213e", fg="#88aacc",
                 font=("Courier", 9, "bold")).pack(anchor=tk.W, padx=8, pady=(6, 2))

    def _btn(self, parent, text, cmd, color="#0f3460"):
        tk.Button(parent, text=text, command=cmd, bg=color, fg="#e0e0e0",
                  activebackground="#1a4a8a", activeforeground="#fff",
                  relief=tk.FLAT, bd=0, font=("Courier", 9),
                  padx=8, pady=4, anchor=tk.W
                  ).pack(fill=tk.X, padx=8, pady=2)

    # ── Image loading ───────────────────────────────────────────────────────
    def load_image(self):
        path = filedialog.askopenfilename(
            filetypes=[("Images", "*.jpg *.jpeg *.png *.bmp *.tiff"), ("All", "*.*")])
        if not path:
            return
        self.clear_all()
        self.image_path = path
        self.pil_image = Image.open(path)
        self._fit_image()
        self.status(f"Loaded: {os.path.basename(path)}")

    def _fit_image(self):
        if not self.pil_image:
            return
        cw = self.canvas.winfo_width() or 900
        ch = self.canvas.winfo_height() or 600
        iw, ih = self.pil_image.size
        scale = min(cw / iw, ch / ih, 1.0)
        self.scale = scale
        nw, nh = int(iw * scale), int(ih * scale)
        self.offset_x = (cw - nw) // 2
        self.offset_y = (ch - nh) // 2
        resized = self.pil_image.resize((nw, nh), Image.LANCZOS)
        self.tk_image = ImageTk.PhotoImage(resized)
        self._redraw()

    def _on_resize(self, event):
        self._fit_image()

    # ── Coordinate helpers ──────────────────────────────────────────────────
    def canvas_to_image(self, cx, cy):
        return ((cx - self.offset_x) / self.scale,
                (cy - self.offset_y) / self.scale)

    def image_to_canvas(self, ix, iy):
        return (ix * self.scale + self.offset_x,
                iy * self.scale + self.offset_y)

    # ── Drawing ─────────────────────────────────────────────────────────────
    def _on_click(self, event):
        if not self.pil_image:
            self.status("⚠ Load an image first!")
            return
        img_pt = self.canvas_to_image(event.x, event.y)
        self.current_points.append(img_pt)

        # Draw point marker
        cx, cy = event.x, event.y
        r = POINT_RADIUS
        pid = self.canvas.create_oval(cx-r, cy-r, cx+r, cy+r,
                                      fill=self._current_color(), outline="#fff", width=1)
        self.current_canvas_ids.append(pid)

        # If line mode and 2 points → auto finish
        if self.draw_mode.get() == "line" and len(self.current_points) == 2:
            self.finish_shape()

        self.status(f"Points: {len(self.current_points)}  |  [{self.draw_mode.get()}]  Press Enter to finish")

    def _on_motion(self, event):
        if not self.current_points:
            return
        
        # Xóa sạch tất cả các nét vẽ tạm thời (cũ) bằng tag
        self.canvas.delete("preview")
        
        last = self.image_to_canvas(*self.current_points[-1])
        color = self._current_color()
        mode = self.draw_mode.get()

        # Vẽ đường nối từ điểm cuối đến chuột
        self.canvas.create_line(
            last[0], last[1], event.x, event.y,
            fill=color, dash=(4, 4), width=2, tags="preview"
        )

        # Nếu là polygon và có từ 2 điểm trở lên, vẽ thêm đường nối về điểm đầu
        if mode == "polygon" and len(self.current_points) >= 2:
            first = self.image_to_canvas(*self.current_points[0])
            self.canvas.create_line(
                event.x, event.y, first[0], first[1],
                fill=color, dash=(2, 4), width=1, tags="preview"
            )

    def _current_color(self):
        label = self.label_var.get()
        return COLORS.get(label, "#ffffff")

    def finish_shape(self):
        mode = self.draw_mode.get()
        if mode == "line" and len(self.current_points) < 2:
            self.status("⚠ Line needs 2 points"); return
        if mode == "polygon" and len(self.current_points) < 3:
            self.status("⚠ Polygon needs ≥ 3 points"); return

        label = self.label_var.get()
        if label == "custom":
            label = self.custom_label_var.get().strip() or "custom"

        color = self._current_color()
        shape = {
            "id": len(self.shapes),
            "type": mode,
            "label": label,
            "points": list(self.current_points),
            "color": color,
            "canvas_ids": []
        }
        self.shapes.append(shape)
        self.current_points = []
        self.current_canvas_ids = []
        if self.rubber_line:
            self.canvas.delete(self.rubber_line)
            self.rubber_line = None
        self.canvas.delete("rubber_close")
        self._redraw()
        self._update_tree()
        self.status(f"✅ Shape saved: {label} ({mode})")

    def cancel_shape(self):
        self.current_points = []
        for cid in self.current_canvas_ids:
            self.canvas.delete(cid)
        self.current_canvas_ids = []
        if self.rubber_line:
            self.canvas.delete(self.rubber_line)
            self.rubber_line = None
        self.canvas.delete("rubber_close")
        self.status("Cancelled")

    def undo_point(self):
        if not self.current_points:
            return
        self.current_points.pop()
        if self.current_canvas_ids:
            self.canvas.delete(self.current_canvas_ids.pop())
        self.status(f"Undo → {len(self.current_points)} points")

    def delete_selected(self):
        if self.selected_shape is None:
            self.status("No shape selected"); return
        self.shapes = [s for s in self.shapes if s["id"] != self.selected_shape]
        self.selected_shape = None
        self._redraw()
        self._update_tree()
        self.status("Shape deleted")

    def clear_all(self):
        if not messagebox.askyesno("Clear All", "Delete all shapes?"):
            return
        self.shapes = []
        self.cancel_shape()
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
        pts = shape["points"]
        color = shape.get("color", "#fff")
        is_selected = (shape["id"] == self.selected_shape)
        outline = "#FFD700" if is_selected else color
        width = 3 if is_selected else 2

        canvas_pts = [self.image_to_canvas(x, y) for x, y in pts]
        flat = [coord for pt in canvas_pts for coord in pt]

        if shape["type"] == "line":
            self.canvas.create_line(*flat, fill=outline, width=width,
                                    arrow=tk.LAST, arrowshape=(10, 12, 4))
        else:
            self.canvas.create_polygon(*flat, outline=outline, fill="",
                                       width=width, dash=() if is_selected else (6, 2))

        # Points
        r = POINT_RADIUS
        for cx, cy in canvas_pts:
            self.canvas.create_oval(cx-r, cy-r, cx+r, cy+r,
                                    fill=color, outline="#fff", width=1)

        # Label
        if canvas_pts:
            lx = sum(p[0] for p in canvas_pts) / len(canvas_pts)
            ly = min(p[1] for p in canvas_pts) - 14
            self.canvas.create_rectangle(lx-4, ly-10, lx + len(shape["label"])*7 + 4, ly+4,
                                         fill="#111111", outline="")
            self.canvas.create_text(lx, ly - 3, text=shape["label"],
                                    fill=color, font=("Courier", 8, "bold"), anchor=tk.W)

    # ── Tree list ───────────────────────────────────────────────────────────
    def _update_tree(self):
        for item in self.tree.get_children():
            self.tree.delete(item)
        for s in self.shapes:
            pts_str = "  ".join(f"({p[0]:.0f},{p[1]:.0f})" for p in s["points"])
            self.tree.insert("", tk.END, values=(s["id"], s["type"], s["label"], pts_str))

    def _on_tree_select(self, event):
        sel = self.tree.selection()
        if not sel:
            return
        vals = self.tree.item(sel[0], "values")
        if vals:
            self.selected_shape = int(vals[0])
            self._redraw()

    # ── JSON ────────────────────────────────────────────────────────────────
    def save_json(self):
        if not self.shapes:
            self.status("Nothing to save"); return
        path = filedialog.asksaveasfilename(defaultextension=".json",
                                            filetypes=[("JSON", "*.json")])
        if not path:
            return
        data = {
            "image": self.image_path,
            "image_width":  self.pil_image.width  if self.pil_image else None,
            "image_height": self.pil_image.height if self.pil_image else None,
            "shapes": [
                {
                    "id": s["id"],
                    "type": s["type"],
                    "label": s["label"],
                    "color": s["color"],
                    "points": [[round(x, 2), round(y, 2)] for x, y in s["points"]],
                }
                for s in self.shapes
            ]
        }
        with open(path, "w", encoding="utf-8") as f:
            json.dump(data, f, indent=2, ensure_ascii=False)
        self.status(f"💾 Saved: {os.path.basename(path)}")

    def load_json(self):
        path = filedialog.askopenfilename(filetypes=[("JSON", "*.json")])
        if not path:
            return
        with open(path, encoding="utf-8") as f:
            data = json.load(f)

        # Auto-load image if path stored
        if data.get("image") and os.path.exists(data["image"]):
            self.image_path = data["image"]
            self.pil_image = Image.open(data["image"])

        self.shapes = []
        for s in data.get("shapes", []):
            self.shapes.append({
                "id": s["id"],
                "type": s["type"],
                "label": s["label"],
                "color": s.get("color", COLORS.get(s["label"], "#fff")),
                "points": [tuple(p) for p in s["points"]],
                "canvas_ids": []
            })

        self._fit_image()
        self._update_tree()
        self.status(f"📥 Loaded {len(self.shapes)} shapes from {os.path.basename(path)}")

    # ── Status ──────────────────────────────────────────────────────────────
    def status(self, msg):
        self.status_var.set(msg)


# ── Entry point ────────────────────────────────────────────────────────────
if __name__ == "__main__":
    root = tk.Tk()
    app = TrafficAnnotator(root)
    root.mainloop()