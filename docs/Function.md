Function Documentation

Calibration
------------------------------------------------
Reads an average input RMS over a certain window for each string input one at a time
Compare each of those averages to the target RMS
Determine a multiplier (per string)by dividing the target RMS by the average input.
create a calibration profile containing these values that then actively boosts the 6 inputs being used for note detection by its designated multiplier. 


Note Detection
------------------------------------------------
