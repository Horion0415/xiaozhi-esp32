# Rhythm Visualizer Component

音乐律动可视化组件，用于在ESP32-C5 Sensairpanel的16×16 LED矩阵上实现音乐律动效果。

## 功能特性

- **自然声音效果**: 
  - 烤火场景：橙红色火焰效果，配合fire.wav音频
  - 下雨场景：蓝色雨滴效果，配合rain.wav音频
  - 海浪场景：蓝绿波浪效果，配合sea.wav音频
- **自动图片显示**: 播放音频时自动显示对应的图片
- **16×16 LED矩阵**: 实时音乐律动灯光效果
- **简单操作**: 点击UI按钮即可循环切换场景

## 使用方法

### UI操作
1. 进入ui_ScreenLight页面
2. 点击"律动"按钮
3. 自动循环播放：烤火 → 下雨 → 海浪 → 停止
4. 播放时会显示对应的图片并LED矩阵同步律动

### 音频文件要求
- fire.wav - 烤火声音，存放在/spiffs/fire.wav
- rain.wav - 下雨声音，存放在/spiffs/rain.wav  
- sea.wav - 海浪声音，存放在/spiffs/sea.wav

### 图片资源
- fire - 烤火图片
- rain - 下雨图片
- sea - 海浪图片

## API接口

### 基础控制
```c
esp_err_t rhythm_visualizer_init(void);
esp_err_t rhythm_start_natural_sound(rhythm_scene_t scene, const char* audio_file);
esp_err_t rhythm_stop(void);
bool rhythm_is_running(void);
```

### 场景类型
```c
typedef enum {
    RHYTHM_SCENE_FIRE = 0,     // 烤火场景
    RHYTHM_SCENE_RAIN,         // 下雨场景  
    RHYTHM_SCENE_WAVE,         // 海浪场景
    RHYTHM_SCENE_SPECTRUM,     // 频谱场景
} rhythm_scene_t;
```

## 技术实现

- **频谱分析**: 256点FFT，分离低/中/高频段
- **LED矩阵**: 16×16 WS2812，RMT驱动
- **音频播放**: WAV文件，16kHz采样率
- **实时渲染**: <50ms延迟，20-60Hz刷新率
- **内存占用**: ~10KB，CPU占用~15%

## 依赖组件

- `esp32_c5_sensairpanel` - BSP支持
- `esp_timer` - 定时器
- `freertos` - 任务管理
- `esp_common` - 基础功能 