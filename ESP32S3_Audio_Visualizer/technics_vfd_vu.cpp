#include "technics_vfd_vu.h"
#include "technics_bg.h"

// ─── Internal State ───────────────────────────────────────────────────────────
static float filtered_rms[2] = {0.0f, 0.0f};
static int last_segments_lit[2] = {0, 0};
static peak_hold_t peak_hold[2] = {{false, 0, 0.0f}, {false, 0, 0.0f}};
static TFT_eSprite *vu_sprites[2] = {nullptr, nullptr};
static bool initialized = false;

// ─── Helper Functions ─────────────────────────────────────────────────────────

// Convert RMS to segment count
static int rms_to_segments(float rms) {
    if (rms <= 0.0f) return 0;
    if (rms >= 1.0f) return VU_MAX_SEGMENTS;
    
    float scaled = log_scale(rms);
    int segments = (int)(scaled * VU_MAX_SEGMENTS);
    return (segments > VU_MAX_SEGMENTS) ? VU_MAX_SEGMENTS : segments;
}

// Get segment X position
static int get_segment_x(int segment) {
    return VU_LEFT_X + segment * (VU_SEGMENT_WIDTH + VU_SEGMENT_GAP);
}

// Draw VU bar in sprite
static void draw_vu_bar(TFT_eSprite &sprite, int segments_lit, int channel) {
    // Clear sprite (copy from background will be done in update)
    sprite.fillSprite(VFD_BLACK);
    
    for (int seg = 0; seg < VU_MAX_SEGMENTS; seg++) {
        int x = seg * (VU_SEGMENT_WIDTH + VU_SEGMENT_GAP);
        
        if (seg < segments_lit) {
            // Determine color based on position (0dB at 80%)
            float seg_pos = (float)seg / (float)VU_MAX_SEGMENTS;
            uint16_t color = (seg_pos >= 0.8f) ? VFD_CYAN_FULL : VFD_AMBER_FULL;
            sprite.fillRect(x, 0, VU_SEGMENT_WIDTH, VU_BAR_HEIGHT, color);
        }
    }
    
    // Draw peak hold if active
    if (peak_hold[channel].active && peak_hold[channel].brightness > 0.0f) {
        int peak_seg = VU_MAX_SEGMENTS - 1;  // Rightmost segment
        int peak_x = get_segment_x(peak_seg);
        
        // Apply brightness to color
        uint16_t base_color = VFD_CYAN_FULL;
        uint16_t dim_color = (peak_hold[channel].brightness > 0.5f) ? base_color : VFD_CYAN_HALF;
        sprite.fillRect(peak_x, 0, VU_SEGMENT_WIDTH, VU_BAR_HEIGHT, dim_color);
    }
}

// Update peak hold state
static void update_peak_hold(int channel, float rms, unsigned long now) {
    int segments = rms_to_segments(rms);
    
    // Activate peak hold if we hit the maximum
    if (segments >= VU_MAX_SEGMENTS - 1) {
        if (!peak_hold[channel].active) {
            peak_hold[channel].active = true;
            peak_hold[channel].start_time = now;
            peak_hold[channel].brightness = 1.0f;
        }
    }
    
    // Update peak hold brightness
    if (peak_hold[channel].active) {
        unsigned long elapsed = now - peak_hold[channel].start_time;
        
        if (elapsed < PEAK_HOLD_MS) {
            // Hold at full brightness
            peak_hold[channel].brightness = 1.0f;
        } else if (elapsed < PEAK_HOLD_MS + PEAK_FADE_MS) {
            // Fade out
            float fade_progress = (float)(elapsed - PEAK_HOLD_MS) / PEAK_FADE_MS;
            peak_hold[channel].brightness = 1.0f - fade_progress;
        } else {
            // Turn off
            peak_hold[channel].active = false;
            peak_hold[channel].brightness = 0.0f;
        }
    }
}

// ─── Public Functions ─────────────────────────────────────────────────────────

void technics_vfd_vu_init(TFT_eSPI &tft) {
    if (initialized) return;
    
    // Draw static background (fallback to black)
    tft.fillScreen(TFT_BLACK);
    // tft.pushImage(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, technics_vu_bg);  // Uncomment when background is ready
    
    // Create sprites for left and right channels
    for (int i = 0; i < 2; i++) {
        vu_sprites[i] = new TFT_eSprite(&tft);
        if (!vu_sprites[i]->createSprite(VU_METER_SPRITE_W, VU_METER_SPRITE_H)) {
            Serial.printf("Failed to create VU sprite %d\n", i);
            return;
        }
        vu_sprites[i]->setSwapBytes(true);
        
        // Initialize state
        filtered_rms[i] = 0.0f;
        last_segments_lit[i] = 0;
        peak_hold[i].active = false;
        peak_hold[i].brightness = 0.0f;
    }
    
    initialized = true;
    Serial.println("Technics VFD VU initialized with peak hold");
}

void technics_vfd_vu_update(TFT_eSPI &tft, float rmsL, float rmsR) {
    if (!initialized) return;
    
    float rms_values[2] = {rmsL, rmsR};
    int positions[2] = {VU_LEFT_Y, VU_RIGHT_Y};
    unsigned long now = millis();
    
    for (int ch = 0; ch < 2; ch++) {
        // Apply EMA ballistics (instant attack, viscous release)
        filtered_rms[ch] = apply_ema(rms_values[ch], filtered_rms[ch], 
                                    VU_ATTACK_ALPHA, VU_RELEASE_ALPHA);
        
        // Convert to segments
        int segments_lit = rms_to_segments(filtered_rms[ch]);
        
        // Update peak hold
        update_peak_hold(ch, filtered_rms[ch], now);
        
        // Check if update needed
        if (segments_lit != last_segments_lit[ch] || peak_hold[ch].active) {
            
            TFT_eSprite &sprite = *vu_sprites[ch];
            
            // Copy background area to sprite (fallback to black)
            sprite.fillSprite(TFT_BLACK);
            // int bg_y = positions[ch];
            // int bg_x = VU_LEFT_X;
            // sprite.pushImage(0, 0, VU_METER_SPRITE_W, VU_METER_SPRITE_H,
            //                &technics_vu_bg[bg_y * SCREEN_WIDTH + bg_x]);  // Uncomment when background is ready
            
            // Draw VU bar
            draw_vu_bar(sprite, segments_lit, ch);
            
            // Push sprite to display
            sprite.pushSprite(VU_LEFT_X, positions[ch]);
            
            // Store for next comparison
            last_segments_lit[ch] = segments_lit;
        }
    }
}

void technics_vfd_vu_cleanup(void) {
    if (!initialized) return;
    
    // Delete sprites
    for (int i = 0; i < 2; i++) {
        if (vu_sprites[i]) {
            vu_sprites[i]->deleteSprite();
            delete vu_sprites[i];
            vu_sprites[i] = nullptr;
        }
    }
    
    initialized = false;
    Serial.println("Technics VFD VU cleanup completed");
}
