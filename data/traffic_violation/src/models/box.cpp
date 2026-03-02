/*******************************************************************************
 * traffic_violation/src/models/box.cpp
 * NMS và IOU − based on R01_object_detection/src/box.cpp (Renesas RZ/V AI SDK)
 ******************************************************************************/
#include "box.h"

float overlap(float x1, float w1, float x2, float w2)
{
    float l1 = x1 - w1 / 2;
    float l2 = x2 - w2 / 2;
    float left  = l1 > l2 ? l1 : l2;
    float r1 = x1 + w1 / 2;
    float r2 = x2 + w2 / 2;
    float right = r1 < r2 ? r1 : r2;
    return right - left;
}

float box_intersection(Box a, Box b)
{
    float w = overlap(a.x, a.w, b.x, b.w);
    float h = overlap(a.y, a.h, b.y, b.h);
    if (w < 0 || h < 0) return 0;
    return w * h;
}

float box_union(Box a, Box b)
{
    float i = box_intersection(a, b);
    return a.w * a.h + b.w * b.h - i;
}

float box_iou(Box a, Box b)
{
    return box_intersection(a, b) / box_union(a, b);
}

void filter_boxes_nms(std::vector<detection>& det, int32_t size, float th_nms)
{
    for (int32_t i = 0; i < size; i++)
    {
        if (det[i].prob == 0) continue;
        Box a = det[i].bbox;
        for (int32_t j = 0; j < size; j++)
        {
            if (i == j || det[i].c != det[j].c) continue;
            Box b = det[j].bbox;
            float inter = box_intersection(a, b);
            if (box_iou(a, b) > th_nms ||
                inter >= a.h * a.w - 1 ||
                inter >= b.h * b.w - 1)
            {
                if (det[i].prob > det[j].prob)
                    det[j].prob = 0;
                else
                    det[i].prob = 0;
            }
        }
    }
}
