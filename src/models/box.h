/*******************************************************************************
 * traffic_violation/src/models/box.h
 * NMS / IOU functions − based on R01_object_detection/src/box.h
 *
 * Box and detection types are defined in common/types.h.
 * This header just declares the NMS helper functions.
 ******************************************************************************/
#pragma once
#include "common/types.h"   /* Box, detection */
#include <vector>

float box_iou          (Box a, Box b);
float overlap          (float x1, float w1, float x2, float w2);
float box_intersection  (Box a, Box b);
float box_union        (Box a, Box b);
void  filter_boxes_nms  (std::vector<detection>& det, int32_t size, float th_nms);
