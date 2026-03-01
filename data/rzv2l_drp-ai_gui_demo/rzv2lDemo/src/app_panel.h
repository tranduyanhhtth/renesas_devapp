/*
 * app_panel.h
 *
 *  Created on: Apr 11, 2023
 *      Author: zkmike
 */

#ifndef SRC_APP_PANEL_H_
#define SRC_APP_PANEL_H_

#include "lvgl/lvgl.h"
#include "app_button.h"

#define NUM_ROWS 3
#define NUM_COLS 2


typedef struct PanelClass {
	lv_obj_t * panel;
	lv_obj_t * rows[NUM_ROWS];
	ButtonClass buttons[NUM_ROWS*NUM_COLS];
	int maxRows;
	int maxCols;
	int maxButtons;
	int cntButtons;
} PanelClass;


void app_CreatePanel ( PanelClass * P, lv_obj_t* ScreenLayout );
void app_addButton( PanelClass *Panel, const char* name, evntFunc event, font_sz_t font_size );


#endif /* SRC_APP_PANEL_H_ */
