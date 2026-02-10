# 2/10 Snap, ESP32-S3 doesn't have classic BT

Initially I went with ESP32-S3 because it got bunch of processing power. But it looks like S3 only have Bluetooth LE which doesn't have A2DP. Gotta went with C3. Currently searching for chips.

# 2/6 TWO USB PORTS? What da hel

Soooo I just realized ESP32-S3 got a native USB, but it seems like we still need USB to UART chip for recovery...? Still digging and I think I will have only one which is native port.

Still researching strapping pins.

# 2/5 Started working on AirNote

Current planning is to use ESP32-S3 as MCU, paired with TI's PCM5102 DAC for high quality audio playback. 

<img width="1654" height="1215" alt="image" src="https://github.com/user-attachments/assets/6e1aefca-a720-47b7-8251-5c6d23f1b340" />

Already has some plan in my mind, currently working on power rails and USB input.
