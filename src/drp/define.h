/*
 * Original Code (C) Copyright Edgecortix, Inc. 2022
 * Modified Code (C) Copyright Renesas Electronics Corporation 2023
 * Adapted for traffic_violation project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 */
/***********************************************************************************************************************
* File Name    : define.h  (traffic_violation – hardware/display constants only)
* Based on     : R01_object_detection/src/define.h v5.00
* Note         : Model-specific params (anchors, labels, thresholds) removed –
*                those are loaded from config.yaml via common/config.h
***********************************************************************************************************************/

#ifndef DEFINE_MACRO_H
#define DEFINE_MACRO_H

/*****************************************
* System Includes
******************************************/
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <errno.h>
#include <vector>
#include <map>
#include <fstream>
#include <iomanip>
#include <cstring>
#include <float.h>
#include <atomic>
#include <semaphore.h>
#include <math.h>
#include <fstream>
#include <sys/time.h>
#include <climits>
#ifdef WITH_DRP
#include <builtin_fp16.h>
#endif
/*Camera control and GUI control*/
#include <opencv2/opencv.hpp>
#include <opencv2/highgui.hpp>

/*****************************************
* Camera / Display dimensions
******************************************/
#ifdef V2H
#define INPUT_CAM_NAME              "USB Camera"
#define IMAGE_OUTPUT_WIDTH          (1920)
#define IMAGE_OUTPUT_HEIGHT         (1080)
#define CHAR_SCALE_LARGE            (1.0)
#define CHAR_SCALE_SMALL            (0.9)
#define CHAR_SCALE_VERY_SMALL       (0.6)
#else /* V2L (default) */
#define DRPAI_MEM_OFFSET            (0x38E0000)
#define INPUT_CAM_NAME              "MIPI Camera"
#define IMAGE_OUTPUT_WIDTH          (1280)
#define IMAGE_OUTPUT_HEIGHT         (720)
#define CHAR_SCALE_LARGE            (0.7)
#define CHAR_SCALE_SMALL            (0.6)
#define CHAR_SCALE_VERY_SMALL       (0.43)
#endif /* V2H */

#define RESIZE_SCALE                (1.5)

/*****************************************
* Camera Capture Parameters
******************************************/
#define CAM_IMAGE_WIDTH             (640)
#define CAM_IMAGE_HEIGHT            (480)
#define CAM_IMAGE_CHANNEL_BGR       (3)
#define CAPTURE_STABLE_COUNT        (8)
#define DRPAI_IN_WIDTH              (CAM_IMAGE_WIDTH)
#define DRPAI_IN_HEIGHT             (CAM_IMAGE_WIDTH)
#define IMAGE_OUTPUT_CHANNEL_BGRA   (4)
#define DRPAI_OUT_WIDTH             (IMAGE_OUTPUT_WIDTH)
#define DRPAI_OUT_HEIGHT            (IMAGE_OUTPUT_HEIGHT)

/*****************************************
* Drawing / Overlay Constants  (used by image.cpp verbatim from R01)
******************************************/
#define CHAR_THICKNESS              (2)
#define CHAR_SCALE_BB               (0.4)
#define CHAR_THICKNESS_BB           (1)
#define LINE_HEIGHT                 (30)
#define LINE_HEIGHT_OFFSET          (20)
#define TEXT_WIDTH_OFFSET           (10)
#define WHITE_DATA                  (0xFFFFFFu)
#define BLACK_DATA                  (0x000000u)
#define GREEN_DATA                  (0x00FF00u)
#define RGB_FILTER                  (0x0000FFu)
#define BOX_LINE_SIZE               (1)
#define BOX_DOUBLE_LINE_SIZE        (1)
#define ALIGHN_LEFT                 (1)
#define ALIGHN_RIGHT                (2)
#define TEXT_START_X                (1440)

/*****************************************
* DRP-AI Frequency
******************************************/
#define DRPAI_FREQ                  (2)

/*****************************************
* Timeout / Timing
******************************************/
#define DRPAI_TIMEOUT               (5)
#define CAPTURE_TIMEOUT             (20)
#define AI_THREAD_TIMEOUT           (20)
#define IMAGE_THREAD_TIMEOUT        (20)
#define DISPLAY_THREAD_TIMEOUT      (20)
#define KEY_THREAD_TIMEOUT          (5)
#define TIME_COEF                   (1)
#define WAIT_TIME                   (1000)

/*****************************************
* Utility
******************************************/
#define SIZE_OF_ARRAY(array) (sizeof(array)/sizeof(array[0]))

#endif /* DEFINE_MACRO_H */
