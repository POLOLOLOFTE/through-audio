# through-audio
a custom audio display for seeing how cool is music! compatible with the #free digital amp:

## Description
this is a machine that converts audio signals in visible immagines like a spectrum (Oscilloscope) or a frequencess dividor so you can see the music and admire the beauty of sound!

the hole mashine is controlled by a esp32 though a custom pcb

and it has got two displays: one with led matrix and the other one is a lcd display RGB 3.5" (for using the code it needs to be touch)

> "Note:" This is not a professional equipment and probably is not precise eather.

### why i made it?
as i sayed in the other projects i love music and it's a partof me, and have the opportunity to see it is a magic things!

## Assembly:
- you have to soder every componets to the pcb in the right order
- then you have to put the ESP32 DevC 38Pin in the slot
- after you could upload the firmware (opsional)
- then i raccomend wire up the display by folowing the pcb and then test is something explode😭🤣(just joking) i mean if it works!
- after this you should put the board on the custom case and rewire (use long wires) the displays (and glue them with hot glue)
- after this if you use the display that i will link you should run two wires from the matrix display's button and connect them to a second one and put the second on the front pannel
- then you should connect the 16v power, the usb (only for power) and the jack for the input
- enjoy!

## Input and Output
it has got only input:
- Jack
- 16V screw connector
- and USB (Esp32)

## Settings
it's not that complex but if you use the Default firmware you got the touch on the main display and the button for changing the mode on the odter matrix display
as the Default code you have 3 buttons that you can press 1 is for the theme (colors) one is for the mode that you want(Oscilloscope, equalizor, equalizor matrix like)

# Gallery:

<img width="302" height="345" alt="Screenshot 2026-11-01 105819" src="https://github.com/user-attachments/assets/27c54adf-59f1-4122-aa23-779f07d42280" />
<img width="854" height="551" alt="Screenshot 2026-11-01 095814" src="https://github.com/user-attachments/assets/7c8f602d-0107-4e0e-8668-218f0f48caef" />
<img width="978" height="656" alt="Screenshot 2026-10-31 185924" src="https://github.com/user-attachments/assets/be6dd6ff-2d11-4475-b6e4-c513140cd9e2" />
<img width="1840" height="812" alt="Screenshot 2026-10-29 175616" src="https://github.com/user-attachments/assets/72f0e739-891e-422e-a634-a68ef14c5d41" />
<img width="1045" height="486" alt="Screenshot 2026-11-01 110322" src="https://github.com/user-attachments/assets/57497b03-f27e-41d0-aa86-590374a72a99" />
<img width="727" height="388" alt="Screenshot 2026-11-01 110645" src="https://github.com/user-attachments/assets/3bacce60-16fb-4172-8f5e-4de314ea404e" />

#Bill Of Materials

| # | Component | Qty | Why it is used | Amazon link | Approx. price (USD) |
|---|-----------|-----|----------------|-------------|---------------------|
| 1 | ESP32 DevKit, 38-pin, USB-C (ESP-WROOM-32) | 1 | Main microcontroller. Samples the audio on ADC1 (GPIO34/35), computes the FFT and draws the three display modes. | [DIYables ESP32 38-pin](https://www.amazon.com/dp/B0GQXZ2RK1) | ~$11 |
| 2 | 3.5" TFT 480x320, ILI9488, SPI, resistive touch (stylus included) | 1 | User interface: oscilloscope, spectrum and stereo LED views, with big touch buttons for mode, color and gain. | [Hosyond 3.5" ILI9488](https://www.amazon.com/dp/B0BWJHK4M6) | ~$18 |
| 3 | Dual 12-level stereo LED VU-meter module (7-12 V) | 1 | Hardware LED level bars. They run on their own chip, so they need no GPIO, only the audio signal and 9 V. | [Dual 12-level VU meter](https://www.amazon.com/dp/B01MSYKM52) | ~$10 |
| 4 | L7809CV 9 V linear regulator, TO-220 (pack of 5) | 1 pack | Steps 16 V down to the 9 V the VU-meter needs (about 0.7 W of heat at 100 mA). | [L7809CV x5](https://www.amazon.com/dp/B08JHGS3LT) | ~$9 |
| 5 | LM2596 buck converter module (adjustable) | 1 | Efficient 16 V to 5 V for the ESP32 (VIN) and the display. Set it to 5.0 V with a multimeter before connecting anything. | [LM2596 buck module](https://www.amazon.com/dp/B01MS4D2FO) | ~$7 |
| 6 | 3.5 mm stereo jack, 5-pin PCB mount PJ-313 (pack of 5) | 1 pack | Audio input (left/right) for the ESP32 ADC and the VU-meter. | [uxcell PJ-313 x5](https://www.amazon.com/dp/B0BJJGXSJP) | ~$6 |
| 7 | 5.08 mm screw terminal blocks, 2-pin and 5-pin kit | 1 kit | 2-pin: 16 V power input. 5-pin: connection to the VU-meter (L, G, R, G, U). | [Terminal block kit](https://www.amazon.com/dp/B0F7Y1XMWT) | ~$11 |
| 8 | Metal film resistor assortment, 1/4 W 1% | 1 kit | 2x 100 kΩ (audio bias divider), 2x 1 kΩ (ADC input protection), 2x 1-10 kΩ (VU-meter inputs). | [BOJACK resistor kit](https://www.amazon.com/dp/B07P3MFG5D) | ~$13 |
| 9 | Electrolytic capacitor assortment (1 µF to 1000 µF) | 1 kit | Input and output smoothing for the 9 V regulator, and the 16 V input reservoir. Use 25 V or higher. | [Electrolytic kit](https://www.amazon.com/dp/B098RMS3S5) | ~$13 |
| 10 | Ceramic capacitor assortment (0.1 µF to 10 µF) | 1 kit | 100 nF decoupling for the regulator, 1-10 µF audio coupling and bias-node capacitors. | [BOJACK ceramic kit](https://www.amazon.com/dp/B07P7HRGT9) | ~$10 |
| 11 | 2.54 mm single-row female header strips, 40-pin (pack of 20) | 1 pack | Sockets for the ESP32 board and the display, so nothing is soldered directly. Cut to length. | [Female headers 1x40](https://www.amazon.com/dp/B0GXKSB3VF) | ~$8 |
| 12 | TO-220 heatsink + mounting kit (optional) | 1 pack | Keeps the L7809 cool if the VU-meter runs for long periods. | [TO-220 heatsink kit](https://www.amazon.com/dp/B078ZXZ1GN) | ~$8 |
