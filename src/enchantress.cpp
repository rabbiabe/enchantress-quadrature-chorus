#include "enchantress.h"

void setup() {
    Serial.begin(115200);
    // comment out the next line because it locked when I tried to run the code without a serial monitor
    // while (!Serial) { /* do nothing */ }
    delay(1000);
    Serial.printf("\n\n\n\nPeddler Effects Enchantress v1.3 up and running\n");

    pinMode(pinDepth, INPUT);
    pinMode(pinRate, INPUT);
    pinMode(pinSpells, INPUT);
    pinMode(pinWave, INPUT_PULLUP);
    pinMode(pinSCL, INPUT_PULLUP);
    pinMode(pinSDA, INPUT_PULLUP);
    pinMode(pinLED, OUTPUT);

    attachInterrupt(pinWave, &waveSelect_cb, CHANGE);
    Serial.printf("> Pins initialized\n");
    
    for (uint16_t wt = 0; wt < 256; wt++) waveTables[SINE][wt] = float2fix15(sine_table_float[wt]);
    Serial.printf("> Sine wavetable loaded\n");
    for (uint8_t r = 0; r < 16; r++) readKnobs(); // preload the average value arrays
    Serial.printf("> Input knobs initialized\n");

    bool dacOK = dac.begin();
    if (dacOK) {
        Serial.printf("> DAC initialized\n");
    } else {
        Serial.printf("!!! DAC failed :(\n");
        blinkError(et_dac_setup);
    }

    init_timer();
    Serial.printf("> timer initialized\n");

    Serial.printf("> Preload noise\n");
    reseedNoise();
    loadNoise();
    Serial.printf("> loaded!\n");

    startup = false;
}

void loop() {
    if (waveChange) {
        waveIncrementSelection();
        waveChange = false;
    }    
    readKnobs(); 

    /* this code was used for early testing 
    
    if (lfo_call) {
        lfo_call = false;
        Serial.printf("%u, %d, %d, %d, %d\n", micros(), dacValue[0], dacValue[1], dacValue[2], dacValue[3]);
    }
    */

    bool writeOK = dac.fastWrite(dacValue[0], dacValue[1], dacValue[2], dacValue[3]);
    if (!writeOK) blinkError(et_dac_write);

    // this line is an easy way to blink the debugging LED with a frequency of LFO / 2 just to show that everything is working
    digitalWrite(pinLED, (counter >> 31));
}

// loads a 256-element array of perlin noise into the NOISE wavetable, then normalizes it to the range of -1.0 to 1.0
void loadNoise()
{
    //Serial.printf("[loadNoise()]\n");
    fix15 perlinBias = map(spells, 0, 4095, perlinBiasMin, perlinBiasMax);
    perlinNoise(perlinBias, waveTables[NOISE]);
    normalizeNoise();
}

// reads the three control knobs Rate, Depth, and Spells and updates the LFO parameters accordingly. 
// Uses a 16-point rolling average to smooth out noisy ADC inputs.
void readKnobs()
{
    static uint32_t accRate[16], accDepth[16], accSpells[16];
    static uint8_t accIndex = 0;
    uint32_t avgRate = 0, avgDepth = 0, avgSpells = 0;
    uint8_t index = accIndex & 0x0F;
    accRate[index] = analogRead(pinRate);
    accDepth[index] = analogRead(pinDepth);
    accSpells[index] = analogRead(pinSpells);

    for (uint8_t a = 0; a < 16; a++) {
        avgRate += accRate[a];
        avgDepth += accDepth[a];
        avgSpells += accSpells[a];
    }
    
    avgRate /= 16;
    avgDepth /= 16;
    avgSpells /= 16;

    // using map() with uint16_t inputs and fix15 outputs will return a fix15 value, saving us a conversion step
    lfoRate = map(avgRate, 0, 4095, lfoMin, lfoMax);

    /*
     * The raw LFO values are between -1.0 to 1.0, so we divide Depth by 2 to get DAC values from -2047 to 2047 
     * and then add 2047 to get DAC values from 0 to 4094. Converting to fix15 first and then dividing by 2 would be 
     * more accurate than dividing by 2 first and then converting to fix15, but we don't need that much precision and this is faster.
     */
    lfoDepth = int2fix15(avgDepth / 2);

    // each wave type does something different with Spells so we just leave it as a raw 0-4095 value and let the LFO callback function handle it
    spells = avgSpells;

    increment = (uint32_t)((fix2float15(lfoRate) * two32) / sampleRate);

    // Sine + Spells modulates the LFO wave with a faster sine wave, so we need to set the modulation sine rate. 
    // If Spells is at its minimum value, these values will all work out to 0 and there will be no modulation.
    if (waveSelection == SINE) {
        uint8_t mod = map(spells, 0, 4095, 0, 16);
        if (mod > 0) {
            modRate = multfix15(lfoRate, int2fix15(mod));
            modIncrement = (uint32_t)((fix2float15(modRate) * two32) / sampleRate);
        }
    }
    /*  this was for early debugging
    if (!startup && (millis() - last_call > 100)) {
        last_call = millis();
        for (uint8_t clr = 0; clr < 100; clr++) Serial.printf("\n");
        Serial.printf("[status index = %2d] Rate = %2.2f Hz, Depth = %4u, spells = %4u\n", (accIndex & 0x0F), fix2float15(lfoRate), fix2int15(lfoDepth), spells);
        for (uint8_t clr = 0; clr < 10; clr++) Serial.printf("\n");
        /*
        Serial.printf("\n[%4u] = ", avgRate);
        for (uint8_t r = 0; r < 16; r++) Serial.printf("%4u + ", accRate[r]);
        Serial.printf("\n[%4u] = ", avgDepth);
        for (uint8_t d = 0; d < 16; d++) Serial.printf("%4u + ", accDepth[d]);
        Serial.printf("\n[%4u] = ", avgSpells);
        for (uint8_t s = 0; s < 16; s++) Serial.printf("%4u + ", accSpells[s]);
        Serial.printf("\n\n\n\n\n");
        */
    //}*/
    accIndex++;
}

// LFO callback function called by the timer
bool lfo_cb(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx)
{
    fix15 lfoMod = one15, scaleUp, scaleDown, mod15;
    uint16_t mod;

    // get lookup table indices for 0º, 90º, 180º, and 270º
    uint8_t index[] = { static_cast<uint8_t>((counter >> 24)), 
                        static_cast<uint8_t>((counter >> 24) + 64), 
                        static_cast<uint8_t>((counter >> 24) + 128), 
                        static_cast<uint8_t>((counter >> 24) + 192) };    

    switch (waveSelection)
    {
    case SINE:
        mod = map(spells, 0, 4095, 0, 16);

        // if mod == 0 the actual modulation value will be 1.0, i.e., no modulation.
        if (mod > 0) lfoMod = divfix15((waveTables[SINE][modCounter >> 24] + one15), two15); 

        // scale the sine wave by Depth and then add 2047 so we get a valid DAC value 0-4095
        for (uint8_t d = 0; d < 4; d++) dacValue[d] = fix2int15(multfix15(waveTables[waveSelection][index[d]], multfix15(lfoDepth, lfoMod))) + 2047;
        break;
        
    case SQUARE:
        // Read Spells value as pulse width
        mod = map(spells, 0, 4095, pulseWidthMin, pulseWidthMax);

        // Check each index value -- if we are below the pulse width setting, output HIGH otherwise output LOW.
        for (uint8_t d = 0; d < 4; d++) dacValue[d] = (index[d] < mod) ? 4095 : 0;
        break;
    
    case RAMP:
        // Read Spells value as center point, so that the middle of Spells produces a triangle wave and the ends produce sawtooth or ramp
        mod15 = int2fix15(map(spells, 0, 4095, 1, (waveTableLength - 2)));

        // determine the step size for ascending and descending directions
        scaleUp =  divfix15(int2fix15(counterMax), mod15);
        scaleDown = divfix15(int2fix15(counterMax), (int2fix15(counterMax) - mod15));

        // if the index is below the center point, we are going up; otherwise we are going down
        for (uint8_t d = 0; d < 4; d++) {
            if (index[d] < fix2int15(mod15)) {
                dacValue[d] = fix2int15(multfix15(multfix15(int2fix15(index[d]), scaleUp), divfix15(dacMax15, counterMax15)));
            } else {
                dacValue[d] = dacMax - fix2int15(multfix15(multfix15((int2fix15(index[d]) - mod15), scaleDown), divfix15(dacMax15, counterMax15)));
            }
        }
        break;
    
    case NOISE:
        // read perlin noise from the array. in this mode Spells is used when loading the noise.
        for (uint8_t d = 0; d < 4; d++) dacValue[d] = fix2int15(multfix15(waveTables[waveSelection][index[d]], multfix15(lfoDepth, lfoMod))) + 2047;
        break;
    
    default:
        break;
    }

    // increment the main and modulation indices.
    counter = counter + increment;
    modCounter = modCounter + modIncrement;

    // this was used for debugging
    //lfo_call = true; 
    return false;
}

// wave select push button changes the selected wave. in NOISE mode, it can be held to reload new noise values.
// to keep the callback short, we just set a flag and let the work happen in loop()
void waveSelect_cb()
{
    if ((waveSelectPressTime > 0) && ((micros() - waveSelectPressTime) < buttonDebounce_us)) return; 
    buttonDown = !digitalRead(pinWave);
    if (buttonDown) {
        waveSelectPressTime = micros();
    } else {
        if ((micros() - waveSelectPressTime) > buttonHold_us) {
            // check for long hold
            if (waveSelection == NOISE) loadNoise();
        } else {
            waveChange = true;
        }
        waveSelectPressTime = 0;
    }
}

// select the next wave type 
void waveIncrementSelection()
{
    switch (waveSelection)
    {
        case SINE:
            waveSelection = RAMP;
            break;
        
        case RAMP:
            waveSelection = SQUARE;
            break;
        
        case SQUARE:
            // when going from SQUARE to NOISE, load new noise values.
            reseedNoise();
            loadNoise();
            waveSelection = NOISE;
            break;
    
        case NOISE:
            waveSelection = SINE;
            break;
    }
    //Serial.printf("    [waveIncrementSelection()] new wave %u\n", waveSelection);
}

// Initialize the timer that controls the LFO
void init_timer()
{
    Serial.printf("[init_timer()]\n");
    esp_err_t err_return;
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT, // Select the default clock source
        .direction = GPTIMER_COUNT_UP,      // Counting direction is up
        .resolution_hz = 1 * 1000 * 1000,   // Resolution is 1 MHz, i.e., 1 tick equals 1 microsecond
    };

    // Create a timer instance
    err_return = gptimer_new_timer(&timer_config, &lfoTimer);
    if (err_return == ESP_OK) {
        Serial.printf(">>> timer instance created\n        Resolution %d\n", timer_config.resolution_hz);
    } else {
        Serial.printf("!!! timer instance failed: %d\n", err_return);
        blinkError(et_new_timer);
    }

    alarm_config.alarm_count = 1000000 / sampleRate; 
    //alarm_config.reload_count = alarm_config.alarm_count;
    alarm_config.flags.auto_reload_on_alarm = true;

    // Set the timer's alarm action
    err_return = gptimer_set_alarm_action(lfoTimer, &alarm_config);
    if (err_return == ESP_OK) {
        Serial.printf(">>> alarm set\n        Count %d\n", alarm_config.alarm_count);
    } else {
        Serial.printf("!!! alarm set failed: %d\n", err_return);
        blinkError(et_set_alarm_action);
    }

    gptimer_event_callbacks_t cbs = {
        .on_alarm = lfo_cb, // Call the user callback function when the alarm event occurs
    };

    // Register timer event callback functions, allowing user context to be carried
    err_return = gptimer_register_event_callbacks(lfoTimer, &cbs, NULL);
    if (err_return == ESP_OK) {
        Serial.printf(">>> callback registered\n");
    } else {
        Serial.printf("!!! register callback failed: %d\n", err_return);
        blinkError(et_register_event_callback);
    }

    // Enable the timer
    err_return = gptimer_enable(lfoTimer);
    if (err_return == ESP_OK) {
        Serial.printf(">>> timer enabled\n");
    } else {
        Serial.printf("!!! enable timer failed: %d\n", err_return);
        blinkError(et_timer_enable);
    }

    // start the timer
    err_return = gptimer_start(lfoTimer);
    if (err_return == ESP_OK) {
        Serial.printf(">>> timer started\n");
    } else {
        Serial.printf("!!! start timer failed: %d\n", err_return);
        blinkError(et_timer_start);
    }


}

// fill noiseSeed[] with a new set of random values scaled -1.0 to 1.0
void inline reseedNoise()
{
    //Serial.printf("[reseedNoise()]\n");
    for (uint16_t s = 0; s < 256; s++) {
        int rn = rand();
        float rn_scaled = static_cast<float>(rn) / static_cast<float>(RAND_MAX); 
        float rn_doubled = rn_scaled * 2.0;
        float rn_posneg = rn_doubled - 1.0;
        noiseSeed[s] = float2fix15(rn_posneg);
    }
}

// fill the fOutput array with perlin noise
void perlinNoise(fix15 fBias, fix15* fOutput)
{
    uint8_t nOctaves = 8;

    for (uint16_t x = 0; x < 256; x++) {
        fix15 fNoise = zero15;
        fix15 fScaleAcc = zero15;
        fix15 fScale = one15;
        int nPitch, nSample1, nSample2;
        fix15 fBlend, fSample;



        for (uint16_t o = 0; o < nOctaves; o++)
        {
            nPitch = 256 >> o;
            nSample1 = (x / nPitch) * nPitch;
            nSample2 = (nSample1 + nPitch) % 256;

            fBlend = divfix15(int2fix15(x - nSample1), int2fix15(nPitch));

            fSample = multfix15((one15 - fBlend), noiseSeed[nSample1]) + multfix15(fBlend, noiseSeed[nSample2]);

            fScaleAcc += fScale;
            fNoise += multfix15(fSample, fScale);
            fScale = divfix15(fScale, fBias);
        }

        // Scale to seed range
        fOutput[x] = divfix15(fNoise, fScaleAcc);
    }
}

// Normalize perlin noise to the range of -1.0 to 1.0 
void normalizeNoise()
{
    Serial.printf("\n\n[normalizeNoise()]\n");
    fix15 min = one15, max = zero15;
    for (uint16_t n = 0; n < 256; n++) {
        if (min > waveTables[NOISE][n]) min = waveTables[NOISE][n]; 
        if (max < waveTables[NOISE][n]) max = waveTables[NOISE][n]; 
    }
    if (min >= max) return;
//    Serial.printf("> Min = %f (%d), Max = %f (%d)\n\n", fix2float15(min), min, fix2float15(max), max);
//    Serial.printf("Raw, Normalized\n");


    for (uint16_t n = 0; n < 256; n++) {
        //Serial.printf("%f,", fix2float15(waveTables[NOISE][n]));
        waveTables[NOISE][n] = mapFix15(waveTables[NOISE][n], min, max, negativeOne15, one15);
        //Serial.printf("%f\n", fix2float15(waveTables[NOISE][n]));
    }

}

// version of map() adapted to work properly with fix15 values
fix15 mapFix15(fix15 x, fix15 in_min, fix15 in_max, fix15 out_min, fix15 out_max) {
    fix15 run = in_max - in_min;
    if(run == 0){
        log_e("map(): Invalid input range, min == max");
        return -1; // AVR returns -1, SAM returns 0
    }
    fix15 rise = out_max - out_min;
    fix15 delta = x - in_min;
    fix15 numerator = multfix15(delta, rise);
    fix15 returnValue = divfix15(numerator, run) + out_min;
    return returnValue;
}

// blinks a specified number of times for a quick indication of error
void blinkError(uint8_t blink_count)
{
    constexpr uint16_t dwellTime = 5000, stepTime = dwellTime / 2;
    if ((blink_count == 0) || (blink_count > 9)) return;
    uint16_t blankSpace = 2000 - (dwellTime * blink_count);
    while (true) {
        for (uint8_t b = 0; b < blink_count; b++) {
            digitalWrite(pinLED, true);
            delay(stepTime);
            digitalWrite(pinLED, false);
            delay(stepTime);
        }
        delay(blankSpace);
    }
}

