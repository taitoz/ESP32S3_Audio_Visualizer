#include "technics_eq.h"
#include "technics_bg.h"  // Background images

// ─── Internal State ───────────────────────────────────────────────────────────
static float filtered_values[EQ_BANDS] = {0.0f};
static int last_segments_lit[EQ_BANDS] = {0};
static int last_half_segments[EQ_BANDS] = {0};
static TFT_eSprite *eq_sprites[EQ_BANDS] = {nullptr};
static bool initialized = false;

// ─── Frequency Band Mapping ───────────────────────────────────────────────────
const float eq_frequencies[EQ_BANDS] = {
    63.0f,    // Sub-bass
    160.0f,   // Bass
    400.0f,   // Low-mid
    1000.0f,  // Mid
    2500.0f,  // Upper-mid
    6300.0f,  // Low-treble
    16000.0f, // Treble
    20000.0f  // Ultra-high
};

// ─── Helper Functions ─────────────────────────────────────────────────────────

// Convert band value (0.0-1.0) to segment count with half-brightness logic
static void calculate_segments(float value, int *full_segments, int *half_segment) {
    if (value <= 0.0f) {
        *full_segments = 0;
        *half_segment = 0;
        return;
    }
    
    // Apply logarithmic scaling
    float scaled_value = log_scale(value);
    float segment_float = scaled_value * EQ_MAX_SEGMENTS;
    
    *full_segments = (int)segment_float;
    float fractional = segment_float - *full_segments;
    
    // Determine half-brightness segment
    if (fractional > 0.75f) {
        *half_segment = *full_segments + 1;  // Full bright next segment
        *full_segments = *full_segments;
    } else if (fractional > 0.25f) {
        *half_segment = *full_segments + 1;  // Half bright next segment
        *full_segments = *full_segments;
    } else {
        *half_segment = 0;  // No half segment
    }
    
    // Clamp values
    if (*full_segments >= EQ_MAX_SEGMENTS) {
        *full_segments = EQ_MAX_SEGMENTS - 1;
        *half_segment = 0;
    }
    if (*half_segment > EQ_MAX_SEGMENTS) {
        *half_segment = EQ_MAX_SEGMENTS;
    }
}

// Get band position on screen
static void get_band_position(int band, int *x, int *y) {
    *x = EQ_START_X + band * (EQ_BAND_WIDTH + EQ_BAND_GAP);
    *y = EQ_START_Y;  // Bottom position
}

// Draw single segment in sprite
static void draw_segment(TFT_eSprite &sprite, int y_offset, uint16_t color) {
    sprite.fillRect(0, y_offset, EQ_BAND_WIDTH, EQ_SEGMENT_HEIGHT, color);
}

// ─── Public Functions ─────────────────────────────────────────────────────────

void technics_eq_init(TFT_eSPI &tft) {
    if (initialized) return;
    
    // Draw static background (fallback to black if background not available)
    tft.fillScreen(TFT_BLACK);
    // tft.pushImage(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, technics_eq_bg);  // Uncomment when background is ready
    
    // Create SRAM sprites for each band
    for (int i = 0; i < EQ_BANDS; i++) {
        eq_sprites[i] = new TFT_eSprite(&tft);
        if (!eq_sprites[i]->createSprite(EQ_BAND_WIDTH, EQ_BAND_SPRITE_H)) {
            Serial.printf("Failed to create EQ sprite %d\n", i);
            return;
        }
        eq_sprites[i]->setSwapBytes(true);
        
        // Initialize state
        filtered_values[i] = 0.0f;
        last_segments_lit[i] = 0;
        last_half_segments[i] = 0;
    }
    
    initialized = true;
    Serial.println("Technics EQ initialized with Dirty Rectangles");
}

void technics_eq_update(TFT_eSPI &tft, const float *bandValues) {
    if (!initialized) return;
    
    for (int band = 0; band < EQ_BANDS; band++) {
        // Apply EMA ballistics
        filtered_values[band] = apply_ema(bandValues[band], filtered_values[band], 
                                         EQ_ATTACK_ALPHA, EQ_RELEASE_ALPHA);
        
        // Calculate segments
        int full_segments, half_segment;
        calculate_segments(filtered_values[band], &full_segments, &half_segment);
        
        // Check if update needed
        if (full_segments != last_segments_lit[band] || 
            half_segment != last_half_segments[band]) {
            
            int band_x, band_y;
            get_band_position(band, &band_x, &band_y);
            
            TFT_eSprite &sprite = *eq_sprites[band];
            
            // Copy background area to sprite (fallback to black)
            sprite.fillSprite(TFT_BLACK);
            // sprite.pushImage(0, 0, EQ_BAND_WIDTH, EQ_BAND_SPRITE_H, 
            //                &technics_eq_bg[band_y * SCREEN_WIDTH + band_x]);  // Uncomment when background is ready
            
            // Draw segments from bottom to top
            for (int seg = 0; seg < EQ_MAX_SEGMENTS; seg++) {
                int y_offset = EQ_BAND_SPRITE_H - (seg + 1) * (EQ_SEGMENT_HEIGHT + EQ_SEGMENT_GAP);
                
                if (seg < full_segments) {
                    // Full bright segment
                    uint16_t color = get_vfd_color(filtered_values[band]);
                    draw_segment(sprite, y_offset, color);
                } else if (seg == half_segment - 1) {
                    // Half bright segment
                    uint16_t color = get_vfd_half_color(filtered_values[band]);
                    draw_segment(sprite, y_offset, color);
                }
                // Else: leave black (background)
            }
            
            // Push sprite to display (DMA if available)
            sprite.pushSprite(band_x, band_y);
            
            // Store for next comparison
            last_segments_lit[band] = full_segments;
            last_half_segments[band] = half_segment;
        }
    }
}

void technics_eq_cleanup(void) {
    if (!initialized) return;
    
    // Delete sprites
    for (int i = 0; i < EQ_BANDS; i++) {
        if (eq_sprites[i]) {
            eq_sprites[i]->deleteSprite();
            delete eq_sprites[i];
            eq_sprites[i] = nullptr;
        }
    }
    
    initialized = false;
    Serial.println("Technics EQ cleanup completed");
}
