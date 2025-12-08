# ============================================================================
# ESP32-HMI 主组件 CMakeLists.txt
# ============================================================================

# UI 文件（EEZ Studio 生成）
set(UI_DIR "./eez_ui")
file(GLOB_RECURSE UI_SRCS ${UI_DIR}/*.c ${UI_DIR}/*.cpp)

idf_component_register(
    SRCS
        # 主入口
        "./main.cpp"
        
        # ============ 驱动层 ============
        # I2C 总线
        "./drivers/i2c/i2c_driver.c"
        
        # GPIO 扩展
        "./drivers/gpio/tca9554.c"
        
        # 显示
        "./drivers/display/st77916.c"
        "./drivers/display/esp_lcd_st77916/esp_lcd_st77916.c"
        
        # 触摸
        "./drivers/touch/cst816.c"
        "./drivers/touch/esp_lcd_touch/esp_lcd_touch.c"
        
        # 音频
        "./drivers/audio/pcm5101.c"
        "./drivers/audio/mic_driver.c"
        "./drivers/audio/audio_processor.cc"
        
        # 电源
        "./drivers/power/bat_driver.c"
        
        # RTC
        "./drivers/rtc/pcf85063.c"
        
        # 存储
        "./drivers/storage/sd_card.c"
        "./drivers/storage/file_system.c"
        
        # ============ 服务层 ============
        # WiFi
        "./services/wifi/wifi_service.c"
        
        # HTTP
        "./services/http/http_client.c"
        
        # AI 语音
        "./services/ai/ai_service.cc"
        
        # 笔记录音
        "./services/note/note_service.c"
        "./services/note/audio_recorder.c"
        
        # ============ UI 层 ============
        "./drivers/lvgl_port/lvgl_driver.c"
        ${UI_SRCS}
        
        # ============ 工具 ============
        "./utils/utils.c"

    INCLUDE_DIRS
        # 驱动层（display 放首位确保使用自定义的 esp_lcd_io_i2c.h）
        "./drivers/display"
        "./drivers/display/esp_lcd_st77916"
        "./drivers/i2c"
        "./drivers/gpio"
        "./drivers/touch"
        "./drivers/touch/esp_lcd_touch"
        "./drivers/audio"
        "./drivers/power"
        "./drivers/rtc"
        "./drivers/storage"
        "./drivers/lvgl_port"
        
        # 服务层
        "./services/wifi"
        "./services/http"
        "./services/ai"
        "./services/note"
        
        # UI 层
        ${UI_DIR}
        
        # 工具和根目录
        "./utils"
        "."

    REQUIRES
        # ESP-IDF 组件
        lvgl
        esp_timer
        esp_lcd
        fatfs
        spi_flash
        nvs_flash
        esp_adc
        esp_wifi
        esp_http_client
        esp_websocket_client
        spiffs
        json
        
        # 第三方组件
        78__esp-opus
        78__esp-opus-encoder
        espressif__esp-sr
)

# 强制在 C++ 文件中首先包含兼容性头文件
target_compile_options(${COMPONENT_LIB} PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:-include${CMAKE_CURRENT_SOURCE_DIR}/drivers/display/esp_lcd_io_i2c.h>"
)
