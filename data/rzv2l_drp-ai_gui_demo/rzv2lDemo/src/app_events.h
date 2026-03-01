/*
 * app_events.h
 *
 *  Created on: Apr 13, 2023
 *      Author: zkmike
 */


#ifndef SRC_APP_EVENTS_H_
#define SRC_APP_EVENTS_H_

#include "lvgl/lvgl.h"

void runYoloV5(lv_event_t * e);

void runYoloVX(lv_event_t * e);

void runtYoloV3(lv_event_t * e);

void runTinyYoloV3(lv_event_t * e);

void runYoloV2(lv_event_t * e);

void runTinyYoloV2(lv_event_t * e);



void runHRNet(lv_event_t * e);



void runHeadCount(lv_event_t * e);

void runLineCrossing(lv_event_t * e);

void runFallDetection(lv_event_t * e);

void runSafetyHelmetDetection(lv_event_t * e);

void runAgeGenderDetection(lv_event_t * e);

void runFaceRecognition(lv_event_t * e);

void runGaze(lv_event_t * e);

void runGesture(lv_event_t * e);


void app_event_objectdetection(lv_event_t * e);

void app_event_pose(lv_event_t * e);

void app_event_pretrainedapps(lv_event_t * e);

#endif /* SRC_APP_EVENTS_H_ */
