#include "LVGL_Driver.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG_LVGL = "LVGL";

lv_disp_draw_buf_t disp_buf;                                                 // contains internal graphic buffer(s) called draw buffer(s)
lv_disp_drv_t disp_drv;                                                      // contains callback functions
lv_indev_drv_t indev_drv;

void example_increase_lvgl_tick(void *arg)
{
    /* Tell LVGL how many milliseconds has elapsed */
    lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS);
}

void example_lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t) drv->user_data;
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;
    
    // copy a buffer's content to a specific area of the display
    esp_err_t ret = esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map);
    
    // 如果传输失败，记录错误但不阻塞（避免死锁）
    if (ret != ESP_OK) {
        ESP_LOGW(TAG_LVGL, "LCD绘制失败: %s (区域: %d,%d -> %d,%d)", esp_err_to_name(ret), offsetx1, offsety1, offsetx2, offsety2);
        // 即使失败也通知LVGL继续，避免卡死
    }
    
    // 移除延迟，让LVGL立即继续处理下一帧
    // 延迟会导致SPI队列堆积，造成传输失败
    // 通知 LVGL 刷新完成
    lv_disp_flush_ready(drv);
}

/*Read the touchpad*/
void example_touchpad_read( lv_indev_drv_t * drv, lv_indev_data_t * data )
{
    uint16_t touchpad_x[5] = {0};
    uint16_t touchpad_y[5] = {0};
    uint8_t touchpad_cnt = 0;

    /* Read touch controller data */
    esp_lcd_touch_read_data(drv->user_data);

    /* Get coordinates */
    bool touchpad_pressed = esp_lcd_touch_get_coordinates(drv->user_data, touchpad_x, touchpad_y, NULL, &touchpad_cnt, 5);

    // printf("CCCCCCCCCCCCC=%d  \r\n",touchpad_cnt);
    if (touchpad_pressed && touchpad_cnt > 0) {
        data->point.x = touchpad_x[0];
        data->point.y = touchpad_y[0];
        data->state = LV_INDEV_STATE_PR;
        // printf("X=%u Y=%u num=%d \r\n", data->point.x, data->point.y,touchpad_cnt);
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
   
}
/* Rotate display and touch, when rotated screen in LVGL. Called when driver parameters are updated. */
void example_lvgl_port_update_callback(lv_disp_drv_t *drv)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t) drv->user_data;

    switch (drv->rotated) {
    case LV_DISP_ROT_NONE:
        // Rotate LCD display
        esp_lcd_panel_swap_xy(panel_handle, false);
        esp_lcd_panel_mirror(panel_handle, true, false);
        break;
    case LV_DISP_ROT_90:
        // Rotate LCD display
        esp_lcd_panel_swap_xy(panel_handle, true);
        esp_lcd_panel_mirror(panel_handle, true, true);
        break;
    case LV_DISP_ROT_180:
        // Rotate LCD display
        esp_lcd_panel_swap_xy(panel_handle, false);
        esp_lcd_panel_mirror(panel_handle, false, true);
        break;
    case LV_DISP_ROT_270:
        // Rotate LCD display
        esp_lcd_panel_swap_xy(panel_handle, true);
        esp_lcd_panel_mirror(panel_handle, false, false);
        break;
    }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
lv_disp_t *disp;
void LVGL_Init(void)
{
    ESP_LOGI(TAG_LVGL, "Initialize LVGL library");
    lv_init();
    
    // 增大缓冲区以减少刷新频率，降低SPI队列压力
    // 缓冲区越大，刷新频率越低，但占用内存越多
    // 可以调整这个值：1/4 = 更大缓冲区（更流畅，但占用更多内存）
    //                 1/8 = 中等缓冲区（平衡）
    //                 1/10 = 较小缓冲区（节省内存，但可能更频繁刷新）
    // 建议：如果有足够PSRAM，使用1/4或1/6；如果内存紧张，使用1/8
    size_t buf_size = (EXAMPLE_LCD_WIDTH * EXAMPLE_LCD_HEIGHT / 4);  // 改为1/4，进一步增大缓冲区
    lv_color_t *buf1 = heap_caps_malloc(buf_size * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    if (buf1 == NULL) {
        // 如果PSRAM分配失败，尝试普通内存
        buf1 = (lv_color_t *)malloc(buf_size * sizeof(lv_color_t));
    }
    assert(buf1);
    
    lv_color_t *buf2 = heap_caps_malloc(buf_size * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    if (buf2 == NULL) {
        // 如果PSRAM分配失败，尝试普通内存
        buf2 = (lv_color_t *)malloc(buf_size * sizeof(lv_color_t));
    }
    assert(buf2);
    
    ESP_LOGI(TAG_LVGL, "LVGL缓冲区大小: %d KB (每个缓冲区)", buf_size * sizeof(lv_color_t) / 1024);
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, buf_size);                              // initialize LVGL draw buffers

    ESP_LOGI(TAG_LVGL, "Register display driver to LVGL");
    lv_disp_drv_init(&disp_drv);                                                                        // Create a new screen object and initialize the associated device
    disp_drv.hor_res = EXAMPLE_LCD_WIDTH;             
    disp_drv.ver_res = EXAMPLE_LCD_HEIGHT;                                                     // Horizontal pixel count
    // disp_drv.rotated = LV_DISP_ROT_90; // 图像旋转                                                            // Vertical axis pixel count
    disp_drv.flush_cb = example_lvgl_flush_cb;                                                          // Function : copy a buffer's content to a specific area of the display
    disp_drv.drv_update_cb = example_lvgl_port_update_callback;                                         // Function : Rotate display and touch, when rotated screen in LVGL. Called when driver parameters are updated. 
    disp_drv.draw_buf = &disp_buf;                                                                      // LVGL will use this buffer(s) to draw the screens contents
    disp_drv.user_data = panel_handle;
    
    ESP_LOGI(TAG_LVGL,"Register display indev to LVGL");                                                  // Custom display driver user data
    disp = lv_disp_drv_register(&disp_drv);     
    
    lv_indev_drv_init ( &indev_drv );
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.disp = disp;
    indev_drv.read_cb = example_touchpad_read;
    indev_drv.user_data = tp;
    lv_indev_drv_register( &indev_drv );

    /********************* LVGL *********************/
    ESP_LOGI(TAG_LVGL, "Install LVGL tick timer");
    // Tick interface for LVGL (using esp_timer to generate 2ms periodic event)
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &example_increase_lvgl_tick,
        .name = "lvgl_tick"
    };
    
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, EXAMPLE_LVGL_TICK_PERIOD_MS * 1000));

}
