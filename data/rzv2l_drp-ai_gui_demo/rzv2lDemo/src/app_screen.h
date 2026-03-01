/*
 * app_screen.h
 *
 *  Created on: Apr 11, 2023
 *      Author: zkmike
 */





#ifndef SRC_APP_SCREEN_H_
#define SRC_APP_SCREEN_H_

#include "lvgl/lvgl.h"
#include "app_panel.h"

typedef struct App_Screen {
	lv_obj_t * screen;
	lv_obj_t * layout;
	lv_obj_t * background;
	lv_obj_t * titles;
	lv_obj_t * title;
	lv_obj_t * logo;
	PanelClass Panel;
} App_Screen;


void create_Screen ( App_Screen *scrn, const char* title );

void register_button( App_Screen *s, char * label, evntFunc event, int indx, font_sz_t font_size );
void AppStart(App_Screen* s );


#endif /* SRC_APP_SCREEN_H_ */
