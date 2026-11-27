#include "MKS_LVGL.h"
#include "TFT_eSPI.h"

#define LV_BUF_SIZE              10 * LV_HOR_RES_MAX
GRBL_CRTL mks_grbl;
LVGL_UI_PAGE_t mks_ui_page;

static lv_disp_buf_t    disp_buf;
static lv_color_t       bmp_public_buf[LV_BUF_SIZE];
// static lv_color_t       bmp_private_buf1[LV_BUF_SIZE]; 

/* Function */
void my_disp_flush(lv_disp_drv_t * disp, const lv_area_t * area, lv_color_t * color_p);
bool my_indev_touch(struct _lv_indev_drv_t * indev_drv, lv_indev_data_t * data);

#if defined(MKS_TS24)
/*
 * TS24 support (2.4" 320x240 panel).
 *
 * The UI is laid out for the 480x320 TS35. Rather than re-laying out every
 * page, LVGL keeps drawing a 480x320 virtual screen and each flushed area is
 * scaled down to the panel: 3 virtual columns -> 2 panel columns and 4 virtual
 * rows -> 3 panel rows, with area averaging so text stays smooth. The rounder
 * keeps every area aligned to those 3x4 blocks. Touch is scaled back up.
 */
#define TS24_W        320
#define TS24_H        240
#define TS24_BLK_X    3
#define TS24_BLK_Y    4

static void ts24_rounder(lv_disp_drv_t * disp_drv, lv_area_t * area);

// Output of one flush: at most LV_BUF_SIZE virtual pixels, scaled by 2/3 * 3/4.
static uint16_t ts24_out_buf[LV_BUF_SIZE / 2 + TS24_W];
#endif

// 1ms
void mks_lvgl_init(void) {
    
    /*
        static lv_disp_buf_t disp_buf_2;
        static lv_color_t buf2_1[LV_HOR_RES_MAX * 20];                       
        static lv_color_t buf2_2[LV_HOR_RES_MAX * 20];                       
        lv_disp_buf_init(&disp_buf_2, buf2_1, buf2_2, LV_HOR_RES_MAX * 10);  
    */
    lv_init();
    // lv_disp_buf_init(&disp_buf, bmp_public_buf, bmp_private_buf1, LV_BUF_SIZE); // Initialize the display buffer
    lv_disp_buf_init(&disp_buf, bmp_public_buf, NULL, LV_BUF_SIZE);  

    /* display driver register */
    lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LV_HOR_RES_MAX;
    disp_drv.ver_res = LV_VER_RES_MAX;
    disp_drv.flush_cb = my_disp_flush;
#if defined(MKS_TS24)
    disp_drv.rounder_cb = ts24_rounder;
#endif
    disp_drv.buffer = &disp_buf;
    lv_disp_drv_register(&disp_drv);

    lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);  
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_indev_touch;
    lv_indev_drv_register(&indev_drv);
}

#if defined(MKS_TS24)
static void ts24_rounder(lv_disp_drv_t * disp_drv, lv_area_t * area) {
    area->x1 = (area->x1 / TS24_BLK_X) * TS24_BLK_X;
    area->x2 = ((area->x2 / TS24_BLK_X) + 1) * TS24_BLK_X - 1;
    area->y1 = (area->y1 / TS24_BLK_Y) * TS24_BLK_Y;
    area->y2 = ((area->y2 / TS24_BLK_Y) + 1) * TS24_BLK_Y - 1;
}

// Per-axis weights for area averaging (each row sums to the block size).
static const uint8_t ts24_wx[2][3] = { { 2, 1, 0 }, { 0, 1, 2 } };          // 3 -> 2, sum 3
static const uint8_t ts24_wy[3][4] = { { 3, 1, 0, 0 }, { 0, 2, 2, 0 }, { 0, 0, 1, 3 } };  // 4 -> 3, sum 4

void my_disp_flush(lv_disp_drv_t * disp, const lv_area_t * area, lv_color_t * color_p) {
    // The rounder guarantees block-aligned areas inside the 480x320 virtual screen.
    uint32_t vw = (area->x2 - area->x1 + 1);
    uint32_t vh = (area->y2 - area->y1 + 1);
    uint32_t bx = vw / TS24_BLK_X;
    uint32_t by = vh / TS24_BLK_Y;
    uint32_t ow = bx * 2;
    uint32_t oh = by * 3;
    uint16_t* out = ts24_out_buf;

    for (uint32_t j = 0; j < by; j++) {
        for (uint8_t oy = 0; oy < 3; oy++) {
            for (uint32_t i = 0; i < bx; i++) {
                for (uint8_t ox = 0; ox < 2; ox++) {
                    uint32_t r = 0, g = 0, b = 0;
                    for (uint8_t dy = 0; dy < TS24_BLK_Y; dy++) {
                        uint8_t wy = ts24_wy[oy][dy];
                        if (wy == 0) continue;
                        const lv_color_t* row = color_p + (j * TS24_BLK_Y + dy) * vw + i * TS24_BLK_X;
                        for (uint8_t dx = 0; dx < TS24_BLK_X; dx++) {
                            uint8_t w = wy * ts24_wx[ox][dx];
                            if (w == 0) continue;
                            r += row[dx].ch.red * w;
                            g += row[dx].ch.green * w;
                            b += row[dx].ch.blue * w;
                        }
                    }
                    // Weights sum to 3 * 4 = 12.
                    lv_color_t c;
                    c.ch.red   = (r + 6) / 12;
                    c.ch.green = (g + 6) / 12;
                    c.ch.blue  = (b + 6) / 12;
                    *out++ = c.full;
                }
            }
        }
    }

    tft.startWrite();
    tft.setAddrWindow(area->x1 / TS24_BLK_X * 2, area->y1 / TS24_BLK_Y * 3, ow, oh);
    tft.pushColors(ts24_out_buf, ow * oh, true);
    tft.endWrite();
    lv_disp_flush_ready(disp);
}
#else
void my_disp_flush(lv_disp_drv_t * disp, const lv_area_t * area, lv_color_t * color_p) {

    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    
#if defined(USE_LCD_DMA)
    tft.pushColorsDMA(&color_p->full, w * h, true);
#else 
    tft.pushColors(&color_p->full, w * h, true);
#endif
    

    tft.endWrite();
    lv_disp_flush_ready(disp);
}
#endif

bool my_indev_touch(struct _lv_indev_drv_t * indev_drv, lv_indev_data_t * data) {

    uint16_t touchX=0, touchY=0;
    static uint16_t last_x = 0;
    static uint16_t last_y = 0;
    boolean touched = tft.getTouch(&touchY, &touchX);

#if defined(MKS_TS24)
    // Same orientation handling as the stock TS24 firmware, then scale the
    // 320x240 panel coordinates up to the 480x320 virtual screen.
    if(touchX > TS24_W) touchX = TS24_W;
    if(touchY > TS24_H) touchY = TS24_H;
    touchX = (uint32_t)(TS24_W - touchX) * TS24_BLK_X / 2;
    touchY = (uint32_t)(TS24_H - touchY) * TS24_BLK_Y / 3;
#else
    if(touchX > 480) {
        touchX = 480;
    }

    touchX = 480 - touchX;
    touchY = 320 - touchY;
#endif
    
    if( touched != false ) {
        last_x = touchX;
        last_y = touchY;
        data->point.x = last_x;
        data->point.y = last_y;
        data->state = LV_INDEV_STATE_PR;
        
        BEEP_ON;
    }
    else {
        data->point.x = last_x;
        data->point.y = last_y;
        data->state = LV_INDEV_STATE_REL;
        BEEP_OFF;
    }
    return false;
}   

#if USE_LV_LOG != 0
/* Serial debugging */
void my_print(lv_log_level_t level, const char * file, uint32_t line, const char * dsc)
{
    Serial.printf("%s@%d->%s\r\n", file, line, dsc);
    Serial.flush();
}
#endif

void mks_grbl_parg_init(void) {

    if(language_select->get() == 0) mks_grbl.language = SimpleChinese;
    else if(language_select->get() == 1) mks_grbl.language = English;
    else if(language_select->get() == 1) mks_grbl.language = Deutsch;
    
    mks_grbl.light_status = GRBL_Light_Off;
    mks_grbl.move_dis = M_10_MM;
    mks_grbl.move_speed = HIGHT_SPEED;
    mks_grbl.bl_status = BL_NONE;               
    mks_grbl.is_mks_ts35_flag = false;
    mks_grbl.wifi_check_status = false;
    mks_grbl.wifi_connect_status = false;
    mks_grbl.power_persen = P_0_PERSEN;
    mks_grbl.mks_sd_status = 0;
    mks_grbl.wifi_back_from = 0;
    mks_grbl.is_need_ref_sd_file = true;
    mks_ui_page.mks_ui_page = MKS_UI_Ready;                     // 开机进入主页的标记
    mks_ui_page.wait_count = DEFAULT_UI_COUNT;                  // 允许10个周期的等待lvgl数据缓冲
    mks_grbl.carve_times = 0;                                   // 雕刻次数
    #if defined(ENABLE_WIFI)
    mks_wifi.wifi_show_page = 1;
    mks_wifi.wifi_scanf_status = wifi_none;
    mks_grbl.wifi_connect_enable = false;
    mks_wifi.begin_scanf_num = 0;
    #endif
}

/* MKS SD FILE */
uint8_t mks_sd_file_init() { 

    return 0;
}

uint8_t mks_get_sd_file_name() { 
    return 0;
}

uint8_t mks_upDir(void) {
    return 0;
}

uint8_t mks_nextDir(void) { 
    return 0;
}

uint8_t mks_operation_File() { 
    return 0;
}

uint8_t mks_stopSD() { 
    return 0;
}

SDState mks_readSD_Status(void) { 
    
    // uint8_t sd_status = SDCARD_IDLE;
    SDState sd_status;
    sd_status = get_sd_state(true);
    return sd_status;
}

float mks_caving_persen(void) { 
    return sd_report_perc_complete();
}
