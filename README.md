# Enchantress Quadrature Chorus

### Peddler Effects ([peddlereffects.com](https://peddlereffects.com))

This repo has the control code for the Peddler Effects Enchantress **Quadrature** Chorus. The code controls the chorus LFO; there is no digital signal processing of the audio path. *As of v1.3 (2026-11-25), the software works in a breadboard test but does not work when the ESP32 is installed in the prototype PCB.* 

## Project Goals

**Enchantress** is my attempt to build a "quadrature chorus" guitar effect pedal: four delay lines modulated from the same source but with each delay's modulation another 90º out of phase from the previous. Put differently, relative to the first delay the others are shifted 90º, 180º, and 270º. It seemed too complicated to try to build on a breadboard or stripboard so I drew up the schematic in this repo and ordered prototype PCBs; since, as of writing this, I haven't gotten the prototype to work, I have no idea if this will actually sound good but it seemed worth a shot.

The chorus circuit itself is based on the Little Angel PT2399 chorus. There are some "guess" values on the schematic for the resistors that inject the LFO into PT2399 pin 2 and the mixing resistors on the output stage. My starting assumption there was that I would need to attenuate each delay line to 25% so that all of them summed together would balance appropriately with the dry signal. 

## Controls

The LFO offers four wave types: sine, square-pulse, ramp-triangle, and noise. A push button, debounced in software, selects the wave type. Rate and Depth knobs control the LFO rate (0.1-10.0 Hz) and depth. A third knob, Spells, has a different function in each mode:

* **Sine:** controls the rate of a second sine wave that modulates the main LFO sine wave. Setting this to 0 turns modulation off. As of v1.3 the modulation rate is always an integer multiple of the LFO rate (1-16x).

* **Square-Pulse:** Sets the pulse width.

* **Ramp-Triangle:** Sets the midpoint; 50% spells will be a triangle wave, at the extremes it will be a sawtooth (upward slant) or ramp (downward slant).

* **Noise:** Spells produces the bias value for generating the perlin noise. Noise can be reloaded with a new bias value by holding the wave select button for at least 1 second.

## Implementation

All LFO math is in 15.16 signed fixed point representation. The sine wave and noise are read from a 256-element wave table, while the square and triangle waves are generated mathematically.

The LFO values are sent to a MCP4728 quad DAC. The 3.3Vpp DAC outputs are buffered by a quad op amp that also offers the possibility of amplifying the LFOs. 
