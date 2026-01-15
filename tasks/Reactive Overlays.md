# UI ENHANCEMENT: ALL-IN-ONE REACTIVE ENERGY OVERLAY
# ==================================================

1. OBJECTIVE:
   Modify the existing note overlays to pulses on pick attack 
   and fades smoothly during sustain/decay.

2. INPUT DATA (Per String):
   - current_mag:  [Atomic Float] The real-time magnitude from CQT.
   - trigger_flag: [Atomic Bool] True on Note-On or Flux Retrigger.

3. LOGIC & MATH (Run every UI Frame):

   A. SUSTAIN/DECAY LAYER:
      // Normalize magnitude to 0.0-1.0 range based on 0.25 target.
      target_base_alpha = clamp(current_mag / 0.25, 0.0, 1.0);
      
      // One-pole smoothing to prevent magnitude jitter:
      // final_alpha = (old_alpha * 0.8) + (new_alpha * 0.2)
      final_base_alpha = (final_base_alpha * 0.8) + (target_base_alpha * 0.2);

   B. ATTACK SPIKE LAYER (The "Glint"):
      if (trigger_flag == true) {
          spike_value = 1.0;   // Instant flash to full brightness
          trigger_flag = false; // Reset flag immediately
      }
      // Exponential decay of the spike (adjust 0.85 for speed)
      spike_value *= 0.85; 

4. RENDERING ATTRIBUTES:
   - Fret_Opacity = final_base_alpha;
   - Fret_Color   = Base_Color + (White_Highlight_Vector * spike_value);

5. EXPECTED BEHAVIOR:
   - PLUCK: Instant white "glint" overlaying the note.
   - SUSTAIN: Smooth fade-out in sync with physical string decay.
   - RETRIGGER: A fresh "glint" appears even if the note is already lit.
   - VIBRATO: The light shimmers/pulses with the player's finger movement.


IMPLEMENTATION EXAMPLE

/**
 * UI REACTIVE OVERLAY COMPONENT
 * Integration: Call this within the UI Render Loop (60-120 FPS)
 */

struct FretVisualState {
    float finalBaseAlpha = 0.0f;
    float spikeValue = 0.0f;
    float baseColor[3] = {0.0f, 0.5f, 1.0f}; // Example: Blue
};

void updateFretVisuals(FretVisualState &state, float currentMag, bool triggered) {
    // 1. Sustain/Decay Logic
    float targetAlpha = currentMag / 0.25f;
    if (targetAlpha > 1.0f) targetAlpha = 1.0f;
    if (targetAlpha < 0.0f) targetAlpha = 0.0f;

    // Smooth movement to prevent flickering
    state.finalBaseAlpha = (state.finalBaseAlpha * 0.8f) + (targetAlpha * 0.2f);

    // 2. Attack Spike Logic
    if (triggered) {
        state.spikeValue = 1.0f;
    }
    // Fade the spike every frame
    state.spikeValue *= 0.85f;
    if (state.spikeValue < 0.01f) state.spikeValue = 0.0f;
}

/**
 * SHADER / RENDER NOTE:
 * Pass 'state.finalBaseAlpha' to the Alpha Uniform.
 * Pass 'state.spikeValue' to a 'Mix' Uniform to blend BaseColor with White.
 */


 ADAPTIVE OVERLAY WIDTH
 - Currently, overlay width is a fixed size which is approriate for the first few frets but starts to overlap the inlays eventually. Have the overlay size adapt to whichever fret it occupies, using the scaling factor of the current overlay width compared to the first fret