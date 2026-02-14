# 2/14 YEAHHHH no ESP32 now

ESP32-C3 doesn't have native USB (USB OTG) so we will stick with RP2350! (Or 2040)

Since ESP32 WROOM needs to use standard SMT, we can save some money through using 2350 or 2040.

# 2/12 Just added two pin headers

Nothing new, just two additional header for GPIOs and UART.

# 2/12 Schematic done!

Basically got three TI's chip to get good audio.

From ESP32-C3, to PCM5102A, to OPA2141, finally to OPA1622. 

![airnote_page-0001](https://github.com/user-attachments/assets/ad539967-d62e-4bcb-8750-f78cb7458635)

But I'm not sure if it will work or not. I get into too much field I'm not familiar with.

# 2/11 Reading datasheet...

Just reading datasheet and drawing schematic

<img width="1577" height="1111" alt="image" src="https://github.com/user-attachments/assets/499de1b0-9e54-4de4-84ae-5d808e2ab6ac" />

# 2/10 Snap, ESP32-S3 doesn't have classic BT

Initially I went with ESP32-S3 because it got bunch of processing power. But it looks like S3 only have Bluetooth LE which doesn't have A2DP. Gotta went with C3. Currently searching for chips.

# 2/6 TWO USB PORTS? What da hel

Soooo I just realized ESP32-S3 got a native USB, but it seems like we still need USB to UART chip for recovery...? Still digging and I think I will have only one which is native port.

Still researching strapping pins.

# 2/5 Started working on AirNote

Current planning is to use ESP32-S3 as MCU, paired with TI's PCM5102 DAC for high quality audio playback. 

<img width="1654" height="1215" alt="image" src="https://github.com/user-attachments/assets/6e1aefca-a720-47b7-8251-5c6d23f1b340" />

Already has some plan in my mind, currently working on power rails and USB input.
