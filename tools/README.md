# Image Conversion Tools

Инструменты для конвертации изображений в C массивы для ESP32.

## Установка зависимостей

```bash
pip install Pillow
```

## Использование

### 1. RGB565 массив (быстро, большой размер)

```bash
python image_to_rgb565.py bg_spectrum.png bg_spectrum > ../ESP32S3_Audio_Visualizer/bg_spectrum.h
python image_to_rgb565.py bg_vu.png bg_vu > ../ESP32S3_Audio_Visualizer/bg_vu.h
```

**Размер**: ~230KB на файл  
**Скорость**: Мгновенная загрузка

### 2. JPG массив (медленнее, компактный)

Сначала конвертируй PNG в JPG (качество 85-90%):
```bash
# Используй GIMP, Photoshop или ImageMagick:
convert bg_spectrum.png -quality 90 bg_spectrum.jpg
convert bg_vu.png -quality 90 bg_vu.jpg
```

Затем конвертируй в массив:
```bash
python jpg_to_array.py bg_spectrum.jpg bg_spectrum_jpg > ../ESP32S3_Audio_Visualizer/bg_spectrum_jpg.h
python jpg_to_array.py bg_vu.jpg bg_vu_jpg > ../ESP32S3_Audio_Visualizer/bg_vu_jpg.h
```

**Размер**: ~20-30KB на файл  
**Скорость**: ~50-100ms декодирование

## Требования к изображениям

- **Размер**: 640x180 пикселей
- **Формат**: PNG или JPG
- **Цвета**: RGB (не RGBA, не индексированные)

## Пример

```bash
# Создай изображение в GIMP/Photoshop (640x180)
# Сохрани как bg_spectrum.png

# Конвертируй в RGB565
python image_to_rgb565.py bg_spectrum.png bg_spectrum > ../ESP32S3_Audio_Visualizer/bg_spectrum.h

# Готово! Теперь в коде:
# #include "bg_spectrum.h"
# sprite.pushImage(0, 0, BG_SPECTRUM_WIDTH, BG_SPECTRUM_HEIGHT, bg_spectrum);
```
