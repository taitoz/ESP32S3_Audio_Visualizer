# Background Images Guide

## Зачем нужны фоновые изображения?

**Преимущества**:
1. ✅ **Экономия памяти** - не нужно загружать шрифты TFT_eSPI
2. ✅ **Быстрее** - одна операция `pushImage` вместо множества `drawString`/`drawLine`
3. ✅ **Красивее** - можно использовать сглаживание, градиенты, кастомные шрифты
4. ✅ **Проще** - дизайн в графическом редакторе, а не в коде

---

## Шаг 1: Создание изображений

### Требования
- **Размер**: 640x180 пикселей (SCREEN_WIDTH x SCREEN_HEIGHT)
- **Формат**: PNG или JPG
- **Фон**: Чёрный (#000000)
- **Цвета**: Cyan (#00FFFF), Amber (#FFBF00), Gray (#555555)

### Что рисовать

#### bg_spectrum.png (Spectrum Analyzer)
```
┌──────────────────────────────────────────────────────────────┐
│  SPECTRUM ANALYZER                                           │ ← Заголовок (cyan)
├──────────────────────────────────────────────────────────────┤
│                                                              │
│ 0dB  ─────────────────────────────────────────────────────  │ ← Линия 0dB (gray)
│                                                              │
│-12   ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─  │ ← Линия -12dB (dim gray)
│                                                              │
│      63  125 250 500  1k  2k  4k  8k  16k                   │ ← Частоты (gray)
└──────────────────────────────────────────────────────────────┘
```

**Координаты** (для точного позиционирования):
- Заголовок: центр X=320, Y=14
- Линия 0dB: Y=~120
- Линия -12dB: Y=~150
- Метки частот: Y=175, X зависит от EQ_X0 и EQ_SEG_W

#### bg_vu.png (VU Meter)
```
┌──────────────────────────────────────────────────────────────┐
│  VU METER - STEREO                                           │ ← Заголовок (cyan)
├──────────────────────────────────────────────────────────────┤
│                                                              │
│ L  ┌────────────────────────────────────────────────────┐   │ ← Канал L
│    │                                                    │   │
│    └────────────────────────────────────────────────────┘   │
│                                                              │
│ R  ┌────────────────────────────────────────────────────┐   │ ← Канал R
│    │                                                    │   │
│    └────────────────────────────────────────────────────┘   │
│    -20  -12   -6    0   +8                                  │ ← Шкала dB (gray)
│     │    │    │    │    │                                   │ ← Риски
└──────────────────────────────────────────────────────────────┘
```

**Координаты**:
- Заголовок: центр X=320, Y=14
- Метка L: X=~20, Y=~70
- Метка R: X=~20, Y=~110
- Шкала dB: Y=~165
- Линия 0dB (amber): X=~400 (вертикальная)

---

## Шаг 2: Конвертация в массивы

### Вариант A: RGB565 массив (быстро, но большой файл)

**Скрипт Python**:
```python
#!/usr/bin/env python3
# image_to_rgb565.py

from PIL import Image
import sys

def rgb888_to_rgb565(r, g, b):
    """Convert RGB888 to RGB565"""
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

def image_to_array(image_path, array_name):
    img = Image.open(image_path).convert('RGB')
    width, height = img.size
    
    print(f"// Auto-generated from {image_path}")
    print(f"// Size: {width}x{height} = {width*height} pixels = {width*height*2} bytes")
    print("#pragma once\n")
    print(f"const uint16_t {array_name}[{width * height}] PROGMEM = {{")
    
    pixels = img.load()
    count = 0
    line = "  "
    
    for y in range(height):
        for x in range(width):
            r, g, b = pixels[x, y]
            rgb565 = rgb888_to_rgb565(r, g, b)
            line += f"0x{rgb565:04X},"
            count += 1
            
            if count % 12 == 0:  # 12 values per line
                print(line)
                line = "  "
    
    if line.strip() != "":
        print(line.rstrip(','))
    
    print("};")
    print(f"\n#define {array_name.upper()}_WIDTH {width}")
    print(f"#define {array_name.upper()}_HEIGHT {height}")

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python image_to_rgb565.py <image.png> <array_name>")
        sys.exit(1)
    
    image_to_array(sys.argv[1], sys.argv[2])
```

**Использование**:
```bash
python image_to_rgb565.py bg_spectrum.png bg_spectrum > bg_spectrum.h
python image_to_rgb565.py bg_vu.png bg_vu > bg_vu.h
```

**Размер файла**: ~230KB каждый (640x180x2 байта)

---

### Вариант B: JPG массив (компактно, медленнее)

**Конвертация JPG в массив**:
```bash
# Сначала сохрани PNG как JPG (качество 85-90%)
# Затем используй xxd или скрипт:

xxd -i bg_spectrum.jpg > bg_spectrum_jpg.h
xxd -i bg_vu.jpg > bg_vu_jpg.h
```

**Или Python скрипт**:
```python
#!/usr/bin/env python3
# jpg_to_array.py

import sys

def jpg_to_array(jpg_path, array_name):
    with open(jpg_path, 'rb') as f:
        data = f.read()
    
    print(f"// Auto-generated from {jpg_path}")
    print(f"// Size: {len(data)} bytes")
    print("#pragma once\n")
    print(f"const uint8_t {array_name}[] PROGMEM = {{")
    
    line = "  "
    for i, byte in enumerate(data):
        line += f"0x{byte:02X},"
        if (i + 1) % 16 == 0:
            print(line)
            line = "  "
    
    if line.strip() != "":
        print(line.rstrip(','))
    
    print("};")
    print(f"\n#define {array_name.upper()}_SIZE {len(data)}")

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python jpg_to_array.py <image.jpg> <array_name>")
        sys.exit(1)
    
    jpg_to_array(sys.argv[1], sys.argv[2])
```

**Использование**:
```bash
python jpg_to_array.py bg_spectrum.jpg bg_spectrum_jpg > bg_spectrum_jpg.h
python jpg_to_array.py bg_vu.jpg bg_vu_jpg > bg_vu_jpg.h
```

**Размер файла**: ~20-30KB каждый (зависит от сжатия)

---

## Шаг 3: Интеграция в код

### Вариант A: RGB565 (рекомендуется для скорости)

**1. Скопируй файлы**:
```
ESP32S3_Audio_Visualizer/
├── bg_spectrum.h
└── bg_vu.h
```

**2. Обнови `technics_vfd.cpp`**:
```cpp
#include "technics_vfd.h"
#include "AXS15231B.h"
#include "settings.h"
#include <TJpg_Decoder.h>

// Background images
#include "bg_spectrum.h"
#include "bg_vu.h"

// ...

void technics_vfd_draw_bg_eq(TFT_eSPI &tft) {
    sprite.fillSprite(TFT_BLACK);
    
    // Load RGB565 background
    sprite.pushImage(0, 0, BG_SPECTRUM_WIDTH, BG_SPECTRUM_HEIGHT, bg_spectrum);
}

void technics_vfd_draw_bg_vu(TFT_eSPI &tft) {
    sprite.fillSprite(TFT_BLACK);
    
    // Load RGB565 background
    sprite.pushImage(0, 0, BG_VU_WIDTH, BG_VU_HEIGHT, bg_vu);
}
```

---

### Вариант B: JPG (рекомендуется для экономии памяти)

**1. Скопируй файлы**:
```
ESP32S3_Audio_Visualizer/
├── bg_spectrum_jpg.h
└── bg_vu_jpg.h
```

**2. Обнови `technics_vfd.cpp`**:
```cpp
#include "technics_vfd.h"
#include "AXS15231B.h"
#include "settings.h"
#include <TJpg_Decoder.h>

// Background images (JPG)
#include "bg_spectrum_jpg.h"
#include "bg_vu_jpg.h"

// ...

void technics_vfd_draw_bg_eq(TFT_eSPI &tft) {
    sprite.fillSprite(TFT_BLACK);
    
    // Decode JPG into sprite
    TJpgDec.drawJpg(0, 0, bg_spectrum_jpg, BG_SPECTRUM_JPG_SIZE);
}

void technics_vfd_draw_bg_vu(TFT_eSPI &tft) {
    sprite.fillSprite(TFT_BLACK);
    
    // Decode JPG into sprite
    TJpgDec.drawJpg(0, 0, bg_vu_jpg, BG_VU_JPG_SIZE);
}
```

**3. Настрой TJpgDec в `setup()`** (если ещё не настроен):
```cpp
// В ESP32S3_Audio_Visualizer.ino, в setup():
TJpgDec.setJpgScale(1);
TJpgDec.setSwapBytes(true);
TJpgDec.setCallback([](int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
    sprite.pushImage(x, y, w, h, bitmap);
    return 1;
});
```

---

## Сравнение вариантов

| Параметр | RGB565 Array | JPG Array |
|----------|--------------|-----------|
| **Размер файла** | ~230KB | ~20-30KB |
| **Скорость загрузки** | Мгновенно | ~50-100ms (декодирование) |
| **Память PROGMEM** | Большая | Малая |
| **Качество** | Идеальное | Хорошее (артефакты сжатия) |
| **Рекомендация** | Если есть место | Если мало памяти |

---

## Проверка результата

После интеграции:

1. **Компиляция**: Проверь, что нет ошибок
2. **Размер прошивки**: Убедись, что влезает в 3MB APP partition
3. **Визуально**: Фон должен загружаться при смене режима
4. **FPS**: Не должно быть задержек (RGB565) или ~50ms задержка (JPG)

---

## Troubleshooting

**Ошибка: "array too large"**
- Используй JPG вместо RGB565
- Или увеличь APP partition в `platformio.ini`

**Ошибка: "TJpgDec not found"**
- Установи библиотеку: `TJpg_Decoder` by Bodmer

**Фон не отображается**
- Проверь `#include` пути
- Проверь `PROGMEM` в массиве
- Проверь callback для TJpgDec

**Фон искажён**
- Проверь `setSwapBytes(true)` для sprite
- Проверь размер изображения (должен быть 640x180)

---

## Пример готового файла bg_spectrum.h

```cpp
// Auto-generated from bg_spectrum.png
// Size: 640x180 = 115200 pixels = 230400 bytes
#pragma once

const uint16_t bg_spectrum[115200] PROGMEM = {
  0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,
  0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,
  // ... (115200 значений)
};

#define BG_SPECTRUM_WIDTH 640
#define BG_SPECTRUM_HEIGHT 180
```

---

## Итого

1. ✅ Создай PNG изображения 640x180 с текстом
2. ✅ Конвертируй в `.h` файлы (RGB565 или JPG)
3. ✅ Добавь `#include` в `technics_vfd.cpp`
4. ✅ Раскомментируй `sprite.pushImage()` или `TJpgDec.drawJpg()`
5. ✅ Компилируй и загружай!

**Результат**: Красивые фоны без шрифтов, быстрая загрузка, экономия памяти.
