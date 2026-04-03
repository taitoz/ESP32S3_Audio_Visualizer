#include "technics_analog.h"
#include "technics_bg.h"
#include <math.h>

// ─── Internal State ───────────────────────────────────────────────────────────
static float filtered_rms[2] = {0.0f, 0.0f};
static float last_angles[2] = {ANALOG_MIN_ANGLE, ANALOG_MIN_ANGLE};
static TFT_eSprite *travel_sprites[ANALOG_TRAVELERS] = {nullptr, nullptr};
static int last_traveler_pos[ANALOG_TRAVELERS] = {-1, -1};
static bool initialized = false;

// ─── Helper Functions ─────────────────────────────────────────────────────────

// Convert RMS value to needle angle
static float rms_to_angle(float rms) {
    if (rms <= 0.0f) return ANALOG_MIN_ANGLE;
    if (rms >= 1.0f) return ANALOG_MAX_ANGLE;
    
    // Logarithmic scaling for dB response
    float db = 20.0f * log10f(rms * rms + 0.001f);  // Add small value to avoid log(0)
    
    // Map dB range (-20 to +6) to angle range
    float normalized = (db + 20.0f) / 26.0f;  // 0.0 = -20dB, 1.0 = +6dB
    normalized = (normalized < 0.0f) ? 0.0f : (normalized > 1.0f) ? 1.0f : normalized;
    
    return ANALOG_MIN_ANGLE + normalized * (ANALOG_MAX_ANGLE - ANALOG_MIN_ANGLE);
}

// Convert angle to needle end position
static void angle_to_position(float angle, int *x, int *y) {
    float rad = angle * M_PI / 180.0f;
    *x = ANALOG_CENTER_X + (int)(ANALOG_NEEDLE_L * cosf(rad));
    *y = ANALOG_CENTER_Y - (int)(ANALOG_NEEDLE_L * sinf(rad));  // Negative for screen coords
}

// Get needle bounding rectangle
static void get_needle_bounds(float angle, int *x, int *y, int *w, int *h) {
    int end_x, end_y;
    angle_to_position(angle, &end_x, &end_y);
    
    // Calculate bounding box of needle line
    *x = (ANALOG_CENTER_X < end_x) ? ANALOG_CENTER_X : end_x;
    *y = (ANALOG_CENTER_Y < end_y) ? ANALOG_CENTER_Y : end_y;
    *w = abs(end_x - ANALOG_CENTER_X) + ANALOG_NEEDLE_W;
    *h = abs(end_y - ANALOG_CENTER_Y) + ANALOG_NEEDLE_W;
    
    // Add margin for safe copying
    *x -= 2;
    *y -= 2;
    *w += 4;
    *h += 4;
    
    // Clamp to screen bounds
    if (*x < 0) *x = 0;
    if (*y < 0) *y = 0;
    if (*x + *w > SCREEN_WIDTH) *w = SCREEN_WIDTH - *x;
    if (*y + *h > SCREEN_HEIGHT) *h = SCREEN_HEIGHT - *y;
}

// Draw needle in sprite
static void draw_needle(TFT_eSprite &sprite, float angle, int offset_x, int offset_y) {
    int end_x, end_y;
    angle_to_position(angle, &end_x, &end_y);
    
    // Adjust for sprite offset
    int sprite_end_x = end_x - offset_x;
    int sprite_end_y = end_y - offset_y;
    int sprite_center_x = ANALOG_CENTER_X - offset_x;
    int sprite_center_y = ANALOG_CENTER_Y - offset_y;
    
    // Draw needle line
    sprite.drawLine(sprite_center_x, sprite_center_y, sprite_end_x, sprite_end_y, VFD_CYAN_FULL);
    
    // Draw center pivot
    sprite.fillCircle(sprite_center_x, sprite_center_y, 3, VFD_AMBER_FULL);
}

// Find best traveler sprite for position
static int find_traveler_sprite(int x, int y) {
    // Simple strategy: use sprite 0 for left half, sprite 1 for right half
    return (x < SCREEN_WIDTH / 2) ? 0 : 1;
}

// ─── Public Functions ─────────────────────────────────────────────────────────

void technics_analog_init(TFT_eSPI &tft) {
    if (initialized) return;
    
    // Draw static background (fallback to black)
    tft.fillScreen(TFT_BLACK);
    // tft.pushImage(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, technics_analog_bg);  // Uncomment when background is ready
    
    // Create traveling sprites
    for (int i = 0; i < ANALOG_TRAVELERS; i++) {
        travel_sprites[i] = new TFT_eSprite(&tft);
        if (!travel_sprites[i]->createSprite(ANALOG_TRAVELER_W, ANALOG_TRAVELER_H)) {
            Serial.printf("Failed to create analog traveler sprite %d\n", i);
            return;
        }
        travel_sprites[i]->setSwapBytes(true);
        last_traveler_pos[i] = -1;
    }
    
    // Initialize state
    filtered_rms[0] = 0.0f;
    filtered_rms[1] = 0.0f;
    last_angles[0] = ANALOG_MIN_ANGLE;
    last_angles[1] = ANALOG_MIN_ANGLE;
    
    initialized = true;
    Serial.println("Technics Analog VU initialized with traveling sprites");
}

void technics_analog_update(TFT_eSPI &tft, float rmsL, float rmsR) {
    if (!initialized) return;
    
    float rms_values[2] = {rmsL, rmsR};
    unsigned long now = millis();
    
    for (int ch = 0; ch < 2; ch++) {
        // Apply EMA ballistics
        filtered_rms[ch] = apply_ema(rms_values[ch], filtered_rms[ch], 
                                    ANALOG_ATTACK_ALPHA, ANALOG_RELEASE_ALPHA);
        
        // Convert to angle
        float angle = rms_to_angle(filtered_rms[ch]);
        
        // Check if update needed (angle changed significantly)
        if (fabsf(angle - last_angles[ch]) > 0.5f) {
            
            // Get needle bounds
            int needle_x, needle_y, needle_w, needle_h;
            get_needle_bounds(angle, &needle_x, &needle_y, &needle_w, &needle_h);
            
            // Find appropriate traveler sprite
            int traveler_idx = find_traveler_sprite(needle_x, needle_y);
            TFT_eSprite &sprite = *travel_sprites[traveler_idx];
            
            // Copy background area to sprite (fallback to black)
            sprite.fillSprite(TFT_BLACK);
            // sprite.pushImage(0, 0, needle_w, needle_h,
            //                &technics_analog_bg[needle_y * SCREEN_WIDTH + needle_x]);  // Uncomment when background is ready
            
            // Restore previous position if needed
            if (last_traveler_pos[traveler_idx] >= 0) {
                int old_x, old_y, old_w, old_h;
                get_needle_bounds(last_angles[ch], &old_x, &old_y, &old_w, &old_h);
                
                if (old_x != needle_x || old_y != needle_y) {
                    // Restore previous position (fallback to black)
                    sprite.fillSprite(TFT_BLACK);
                    // sprite.pushImage(0, 0, old_w, old_h,
                    //                &technics_analog_bg[old_y * SCREEN_WIDTH + old_x]);  // Uncomment when background is ready
                    sprite.pushSprite(old_x, old_y);
                }
            }
            
            // Draw new needle
            draw_needle(sprite, angle, needle_x, needle_y);
            
            // Push sprite to display
            sprite.pushSprite(needle_x, needle_y);
            
            // Store for next frame
            last_angles[ch] = angle;
            last_traveler_pos[traveler_idx] = ch;
        }
    }
}

void technics_analog_cleanup(void) {
    if (!initialized) return;
    
    // Delete traveling sprites
    for (int i = 0; i < ANALOG_TRAVELERS; i++) {
        if (travel_sprites[i]) {
            travel_sprites[i]->deleteSprite();
            delete travel_sprites[i];
            travel_sprites[i] = nullptr;
        }
    }
    
    initialized = false;
    Serial.println("Technics Analog VU cleanup completed");
}
