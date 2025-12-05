set(UI_DIR "./eez_ui")
file(GLOB_RECURSE UI_SRCS ${UI_DIR}/*.c ${UI_DIR}/*.cpp) 

idf_component_register(
                         SRCS 
                              "./main.cpp" 
                              "./EXIO/TCA9554PWR.c"
                              "./Audio_Driver/PCM5101.c" 
                              "./MIC_Driver/MIC_Speech.c" 
                              "./LCD_Driver/esp_lcd_st77916/esp_lcd_st77916.c" 
                              "./LCD_Driver/ST77916.c"
                              "./Touch_Driver/esp_lcd_touch/esp_lcd_touch.c"        
                              "./Touch_Driver/CST816.c"  
                              "./LVGL_Driver/LVGL_Driver.c"
                              "./LVGL_UI/LVGL_Example.c"
                              "./LVGL_UI/LVGL_Music.c"
                              "./SD_Card/SD_MMC.c" 
                              "./I2C_Driver/I2C_Driver.c"
                              "./PCF85063/PCF85063.c"
                              "./BAT_Driver/BAT_Driver.c"
                              "./Wireless/Wireless.c"
                              ${UI_SRCS}

                         INCLUDE_DIRS 
                              "./LCD_Driver"  # Put LCD_Driver first to ensure our esp_lcd_io_i2c.h is used
                              "./Audio_Driver" 
                              "./MIC_Driver" 
                              "./LCD_Driver/esp_lcd_st77916" 
                              "./Touch_Driver/esp_lcd_touch"     
                              "./Touch_Driver"
                              "./LVGL_Driver" 
                              "./LVGL_UI" 
                              "./EXIO"
                              "./SD_Card" 
                              "./I2C_Driver"
                              "./PCF85063"
                              "./BAT_Driver"
                              "./Wireless"
                              ${UI_DIR}
                              "."
                         
                         REQUIRES 
                              lvgl
                              esp_timer
                              esp_lcd
                              fatfs
                              spi_flash
                              nvs_flash
                              esp_adc
                              esp_wifi
                              bt
                              espressif__esp-sr
                       )

# Force include our compatibility header first for all C++ files
# This ensures our version is always included before ESP-IDF's version
target_compile_options(${COMPONENT_LIB} PRIVATE 
    "$<$<COMPILE_LANGUAGE:CXX>:-include${CMAKE_CURRENT_SOURCE_DIR}/LCD_Driver/esp_lcd_io_i2c.h>"
)
