# 2/27 Nah LM27762 you come back

So basically, I've done following:

1. Relocated LM27762 to far corner from analog circuit
2. Made some cool branding
3. Rerouted several parts

<img width="421" height="1075" alt="image" src="https://github.com/user-attachments/assets/b632c1d7-4511-4f32-9bc4-d69e4f5b3302" />

# 2/25 Getting rid of LM27762

Posted the design in KiCad Discord server, and I got some feedback. The most important one is LM27762 is VERY noisy and picky about PCB design from my research. I'm gonna upgrade (Kinda of) and make both AMP run in BTL mode.

<img width="822" height="912" alt="image" src="https://github.com/user-attachments/assets/50e451c0-9c57-4b39-8451-0a592cada16c" />

# 2/23 Airnote is now pumper

I initially named it Airnote because I want it to me a portable Bluetooth DAC. But it seems like it's not possible and it's now just a USB DAC.

But I finished the schematic and PCB routing!

![pumper_page-0001](https://github.com/user-attachments/assets/9b66d4d0-3a07-4ef3-8897-47add342ec57)
![pumper_page-0002](https://github.com/user-attachments/assets/a6e22ff1-c22c-4aaa-a048-2cf3c4da49d9)
![pumper_page-0003](https://github.com/user-attachments/assets/205a3835-ac56-4dd4-8beb-c82a06feb3e8)

<img width="461" height="1222" alt="image" src="https://github.com/user-attachments/assets/825b45a6-9812-4633-bb84-fee97516b045" />

# 2/22 It's been a big day

Well I threw my schematic into an AI tool and it told me... OPA2141 is too weak (well I mean tl;dr)

<img width="1591" height="1306" alt="image" src="https://github.com/user-attachments/assets/b710e3d1-9ce5-403d-9a93-d21896a03e61" />

Going to replace it with OPA1652

# 2/22 Completed whole routing

Yayyy, but I'm still not sure wether this board will work or not...

<img width="499" height="1229" alt="image" src="https://github.com/user-attachments/assets/cc035b9e-1de3-4c9e-807e-66b037ab3941" />

# 2/22 Completed power and OPA2141 routing

The power is harder than I was expecting. The chip is so small...

<img width="458" height="1040" alt="image" src="https://github.com/user-attachments/assets/67a569ea-afb5-42c0-9bb6-06fcf9e855a2" />

# 2/22 Completed RP2354 routing

<img width="1446" height="913" alt="image" src="https://github.com/user-attachments/assets/03a31884-0be3-4464-bd34-3534e839da2b" />

# 2/21 Finished all schematic

![airnote_page-0001](https://github.com/user-attachments/assets/c8b83c06-4f43-4283-b88d-f740532d325d)
![airnote_page-0002](https://github.com/user-attachments/assets/1e2606b7-b5af-4237-b735-d22856f6136b)
![airnote_page-0003](https://github.com/user-attachments/assets/2f5f1ae7-694d-4606-b682-078633e2dca6)

# 2/21 RP2354A MCU schematic done!

<img width="1256" height="955" alt="image" src="https://github.com/user-attachments/assets/4415ba74-bd70-431d-9a40-1be936211e1e" />

# 2/21 Back from Lunar New Year holiday

Finally decided to use RP2354A (The one with built in flash), and splitted the design into 3 schematics.

<img width="592" height="583" alt="image" src="https://github.com/user-attachments/assets/dcd2952d-9bb8-4a62-aec2-b7e8de2b6889" />

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
