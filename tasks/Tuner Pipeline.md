- create a pYin pipeline directly out of calibrated input specifically for the tuner mode, bypassing the CQT path for speed and precise mono pitch detection for open notes. Specifications:
1. BUFFER STRATEGY:
   - Use a 2048 or 4096 sample window for pYIN. 
   - Larger windows = better low-frequency (Low E) accuracy.
   - On Pi 5, this latency is negligible for a tuner UI.

2. SUB-CENT CALCULATION:
   - float targetHz = NoteFrequencyTable[detectedNote];
   - float centsOff = 1200.0 * log2(pitchHz / targetHz);
   
3. VISUAL SMOOTHING (EXPONENTIAL DECAY):
   - Do NOT map 'centsOff' directly to the needle position.
   - Use: needlePos = (alpha * currentCents) + ((1-alpha) * lastPos);
   - Set alpha to 0.2 for a "heavy", premium-feeling needle.

4. TARGET "LOCK" STATE:
   - When |centsOff| < 3.0 for more than 500ms:
     * Change Indicator Circle color to Signature Green (#37C38B).