/*
 * app_screen.cpp
 *
 *  Created on: Apr 11, 2023
 *      Author: zkmike
 */



#include "lvgl/lvgl.h"
#include "app_screen.h"

extern const lv_img_dsc_t ui_img_race_robot_1280_720_png;
extern const lv_img_dsc_t ui_img_renesas_logomark_l_png;

lv_obj_t * app_screen;
lv_obj_t * app_layout;
lv_obj_t * app_background;
lv_obj_t * app_titles;
lv_obj_t * app_title;
lv_obj_t * app_logo;


void create_Screen ( App_Screen *scrn, const char* title ) {

	app_screen = lv_obj_create(NULL);
	lv_obj_clear_flag(app_screen, LV_OBJ_FLAG_SCROLLABLE);      /// Flags

	app_background = lv_img_create(app_screen);
	lv_img_set_src(app_background, &ui_img_race_robot_1280_720_png);
	lv_obj_set_width(app_background, LV_SIZE_CONTENT);   /// 1
	lv_obj_set_height(app_background, LV_SIZE_CONTENT);    /// 1
	lv_obj_set_align(app_background, LV_ALIGN_CENTER);
	lv_obj_add_flag(app_background, LV_OBJ_FLAG_ADV_HITTEST);     /// Flags
	lv_obj_clear_flag(app_background, LV_OBJ_FLAG_SCROLLABLE);      /// Flags

	app_layout = lv_obj_create(app_screen);
	lv_obj_set_width(app_layout, 871);
	lv_obj_set_height(app_layout, 630);
	// This position is require. FLex Postion is enabled for all children
	lv_obj_set_x(app_layout, -202);
	lv_obj_set_y(app_layout, -41);
	lv_obj_set_align(app_layout, LV_ALIGN_CENTER);
	lv_obj_set_flex_flow(app_layout, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(app_layout, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
	lv_obj_clear_flag(app_layout, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
	lv_obj_set_style_bg_color(app_layout, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_bg_opa(app_layout, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_border_color(app_layout, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_border_opa(app_layout, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

	app_titles = lv_obj_create(app_layout);
	lv_obj_set_width(app_titles, 882);
	lv_obj_set_height(app_titles, 233);
	// Position determined by Flex and Parent Layout
	lv_obj_set_align(app_titles, LV_ALIGN_CENTER);
	lv_obj_set_flex_flow(app_titles, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(app_titles, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
	lv_obj_clear_flag(app_titles, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
	lv_obj_set_style_bg_color(app_titles, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_bg_opa(app_titles, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_border_color(app_titles, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_border_opa(app_titles, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

	app_logo = lv_img_create(app_titles);
	lv_img_set_src(app_logo, &ui_img_renesas_logomark_l_png );
	lv_obj_set_width(app_logo, LV_SIZE_CONTENT);   /// 1
	lv_obj_set_height(app_logo, LV_SIZE_CONTENT);    /// 1
	// Position determined by Flex and Parent Layout
	lv_obj_set_align(app_logo, LV_ALIGN_CENTER);
	lv_obj_add_flag(app_logo, LV_OBJ_FLAG_ADV_HITTEST);     /// Flags
	lv_obj_clear_flag(app_logo, LV_OBJ_FLAG_SCROLLABLE);      /// Flags

	app_title = lv_label_create(app_titles);
	lv_obj_set_width(app_title, LV_SIZE_CONTENT);   /// 1
	lv_obj_set_height(app_title, LV_SIZE_CONTENT);    /// 1
	lv_obj_set_x(app_title, -335);
	lv_obj_set_y(app_title, -146);
	lv_obj_set_align(app_title, LV_ALIGN_CENTER);
	lv_label_set_text(app_title, title );
	lv_obj_set_style_text_color(app_title, lv_color_hex(0x808080), LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_text_opa(app_title, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_text_font(app_title, &lv_font_montserrat_36, LV_PART_MAIN | LV_STATE_DEFAULT);

	scrn->screen = app_screen;
	scrn->background = app_background;
	scrn->layout = app_layout;
	scrn->titles = app_titles;
	scrn->logo = app_logo;
	scrn->title = app_title;
	scrn->Panel.panel = NULL;

}

void register_button( App_Screen *s, char * label, evntFunc event, int indx, font_sz_t font_size ) {

	PanelClass * panel = &(s->Panel);

	if ( panel->panel == NULL ) {
		app_CreatePanel ( panel, s->layout );
	}

	app_addButton( panel, label, event, font_size );

}

void AppStart(App_Screen* s) {
	 lv_disp_load_scr(s->screen);
}

