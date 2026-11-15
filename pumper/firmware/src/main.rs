#![no_std]
#![no_main]

mod audio;
mod boot;
mod clock_plan;
#[cfg(feature = "diagnostics")]
mod diagnostics;
mod flash;
mod hardware;
mod hid_progress;
mod i2s;
mod led;
mod service;
mod shared;
mod telemetry;
mod usb;

use core::{cell::RefCell, sync::atomic::Ordering};
use embassy_futures::{join::join4, yield_now};
use embassy_rp::{
    bind_interrupts,
    peripherals::USB,
    usb::{Driver, InterruptHandler},
};
use embassy_time::{Duration, Instant, Timer};
use embassy_usb::{
    descriptor::{SynchronizationType, UsageType},
    driver::{Endpoint, EndpointIn, EndpointOut, EndpointType},
};
use shared::SHARED;
use static_cell::StaticCell;

bind_interrupts!(struct Irqs {USBCTRL_IRQ => InterruptHandler<USB>; ADC_IRQ_FIFO => embassy_rp::adc::InterruptHandler;});
static BLOCKS: StaticCell<[audio::Block; 6]> = StaticCell::new();
static READY: StaticCell<audio::Queue> = StaticCell::new();
static FREE: StaticCell<audio::Queue> = StaticCell::new();
static STACK: StaticCell<embassy_rp::multicore::Stack<32768>> = StaticCell::new();

pub static SPECTRUM: pumper_core::spectrum::Pipe = pumper_core::spectrum::Pipe::new();
static CONFIGS: StaticCell<audio::ConfigQueue> = StaticCell::new();
static METERS: StaticCell<audio::MeterQueue> = StaticCell::new();
static FLASH_READY: StaticCell<flash::Queue> = StaticCell::new();
static FLASH_DONE: StaticCell<flash::Queue> = StaticCell::new();
static FLASH_JOB: StaticCell<flash::Job> = StaticCell::new();
static SERVICE: StaticCell<service::Service> = StaticCell::new();

#[cortex_m_rt::entry]
fn main() -> ! {
    let mut config = embassy_rp::config::Config::default();
    config.clocks.xosc.as_mut().unwrap().sys_pll = Some(embassy_rp::clocks::PllConfig {
        refdiv: 1,
        fbdiv: 110,
        post_div1: 6,
        post_div2: 1,
    });
    // Fractional division still contains 220 MHz cycles; retain 1.2 V in
    // every state, settled before the HAL starts the fixed 1320/6 MHz PLL.
    config.clocks.core_voltage = embassy_rp::clocks::CoreVoltage::V1_20;
    config.clocks.voltage_stabilization_delay_us = Some(1000);
    let p = embassy_rp::init(config);
    hardware::set_clock(false, 48_000, pumper_core::format::Format::Pcm16);
    // Bootstrap disabled interrupts while copying; HAL has now installed handlers.
    unsafe {
        cortex_m::interrupt::enable();
    }
    hardware::led_init();
    let serial = hardware::serial();
    let serial = core::str::from_utf8(&serial).unwrap();
    let (mut ready_tx, ready_rx) = READY.init(audio::Queue::new()).split();
    let (mut free_tx, mut free_rx) = FREE.init(audio::Queue::new()).split();
    for block in BLOCKS.init([const { audio::Block::new() }; 6]) {
        free_tx.enqueue(block).ok().unwrap();
    }
    let store = pumper_core::storage::Store::load(&mut flash::SettingsFlash)
        .expect("bounded settings read");
    let state = RefCell::new(usb::State::new(store));
    let runtime = state.borrow().controller.runtime;
    let (config_tx, config_rx) = CONFIGS.init(audio::ConfigQueue::new()).split();
    let (meter_tx, meter_rx) = METERS.init(audio::MeterQueue::new()).split();
    let (flash_send, flash_work) = FLASH_READY.init(flash::Queue::new()).split();
    let (flash_done, flash_receive) = FLASH_DONE.init(flash::Queue::new()).split();
    let (capture, consumer) = SPECTRUM.split().expect("single spectrum producer/consumer");
    let resources = (
        p.PIO0, p.DMA_CH0, p.DMA_CH1, p.PIN_19, p.PIN_20, p.PIN_21, p.FLASH,
    );
    let stack = STACK.init(embassy_rp::multicore::Stack::new());
    #[cfg(feature = "diagnostics")]
    diagnostics::prepare_core1(stack);
    embassy_rp::multicore::spawn_core1(p.CORE1, stack, move || {
        let _owned = resources;
        audio::core1(
            ready_rx,
            free_tx,
            config_rx,
            meter_tx,
            (flash_work, flash_done),
            capture,
            runtime,
        )
    });
    let service = SERVICE.init_with(|| {
        service::Service::new(
            config_tx,
            meter_rx,
            flash_send,
            flash_receive,
            FLASH_JOB.init(flash::Job::new()),
            consumer,
            Instant::now().as_micros(),
        )
    });
    let mut adc = embassy_rp::adc::Adc::new(p.ADC, Irqs, embassy_rp::adc::Config::default());
    let mut temperature = embassy_rp::adc::Channel::new_temp_sensor(p.ADC_TEMP_SENSOR);
    let mut control = usb::Control(&state);
    let mut cfg = embassy_usb::Config::new(0x2e8a, 0xf10a);
    cfg.device_release = 0x0300;
    cfg.composite_with_iads = false;
    cfg.manufacturer = Some("LightWolf");
    cfg.product = Some("Pumper USB DAC");
    cfg.serial_number = Some(serial);
    let mut descriptors = [0u8; 512];
    let mut bos = [0u8; 64];
    let mut msos = [0u8; 8];
    let mut ctrl_buf = [0u8; 128];
    let mut builder = embassy_usb::Builder::new(
        Driver::new(p.USB, Irqs),
        cfg,
        &mut descriptors,
        &mut bos,
        &mut msos,
        &mut ctrl_buf,
    );
    builder.handler(&mut control);
    // Internal descriptor topology drives endpoint allocation. The control handler
    // supplies the byte-identical public C descriptors, including host variants.
    let (mut audio_out, mut feedback_in, mut hid_out, mut hid_in) = {
        let mut function = builder.function(1, 0, 0);
        function.interface().alt_setting(1, 1, 0x20, None);
        let mut interface = function.interface();
        interface.alt_setting(1, 2, 0x20, None);
        let (out, feedback) = {
            let mut alt = interface.alt_setting(1, 2, 0x20, None);
            let out = alt.alloc_endpoint_out(EndpointType::Isochronous, Some(0x01.into()), 776, 1);
            let feedback =
                alt.alloc_endpoint_in(EndpointType::Isochronous, Some(0x81.into()), 4, 1);
            alt.endpoint_descriptor(
                out.info(),
                SynchronizationType::Asynchronous,
                UsageType::DataEndpoint,
                &[],
            );
            alt.endpoint_descriptor(
                feedback.info(),
                SynchronizationType::NoSynchronization,
                UsageType::FeedbackEndpoint,
                &[],
            );
            (out, feedback)
        };
        let mut alt = interface.alt_setting(1, 2, 0x20, None);
        alt.endpoint_descriptor(
            out.info(),
            SynchronizationType::Asynchronous,
            UsageType::DataEndpoint,
            &[],
        );
        alt.endpoint_descriptor(
            feedback.info(),
            SynchronizationType::NoSynchronization,
            UsageType::FeedbackEndpoint,
            &[],
        );
        let mut hid = function.interface();
        let mut alt = hid.alt_setting(3, 0, 0, None);
        let hid_out = alt.endpoint_interrupt_out(Some(0x02.into()), 64, 1);
        let hid_in = alt.endpoint_interrupt_in(Some(0x82.into()), 64, 1);
        (out, feedback, hid_out, hid_in)
    };
    let mut device = builder.build();
    #[cfg(feature = "diagnostics")]
    let mut stack_scan = diagnostics::StackScan::new(false);
    embassy_futures::block_on(join4(
        device.run(),
        async {
            let mut packet = [0u8; 776];
            loop {
                audio_out.wait_enabled().await;
                match audio_out.read(&mut packet).await {
                    Ok(len) => {
                        SHARED.packets.fetch_add(1, Ordering::Relaxed);
                        let stream = state.borrow().stream;
                        if stream.alternate == 0
                            || len == 0
                            || SHARED.flash_busy.load(Ordering::Acquire)
                        {
                            continue;
                        }
                        let block = loop {
                            if let Some(block) = free_rx.dequeue() {
                                break block;
                            }
                            SHARED.backpressure.fetch_add(1, Ordering::Relaxed);
                            yield_now().await;
                        };
                        match pumper_core::format::decode(
                            &mut block.samples,
                            &packet[..len],
                            stream.format,
                        ) {
                            Ok(frames) => {
                                block.frames = frames;
                                block.generation = stream.generation;
                                block.format = stream.format;
                            }
                            // Return invalid packets through core1, preserving SPSC ownership.
                            Err(_) => {
                                block.frames = 0;
                                block.generation = stream.generation.wrapping_sub(1);
                            }
                        }
                        crate::shared::owned(ready_tx.enqueue(block));
                    }
                    Err(_) => yield_now().await,
                }
            }
        },
        async {
            let mut feedback = pumper_core::feedback::Feedback::new(48000);
            let mut generation = u32::MAX;
            loop {
                feedback_in.wait_enabled().await;
                let (stream, macos) = {
                    let s = state.borrow();
                    (s.stream, s.host.host() == pumper_core::usb::Host::Macos)
                };
                if stream.generation != generation {
                    feedback = pumper_core::feedback::Feedback::new(stream.rate);
                    generation = stream.generation;
                }
                let value = feedback.update(SHARED.buffered.load(Ordering::Acquire));
                SHARED.feedback.store(value, Ordering::Relaxed);
                let (bytes, len) = pumper_core::feedback::Feedback::packet(value, macos);
                let deadline = Instant::now() + Duration::from_millis(1);
                let _ = feedback_in.write(&bytes[..len]).await;
                Timer::at(deadline).await;
            }
        },
        join4(
            async {
                let mut report = [0u8; 64];
                loop {
                    hid_out.wait_enabled().await;
                    if let Ok(len) = hid_out.read(&mut report).await {
                        state.borrow_mut().enqueue(&report[..len]);
                    }
                }
            },
            async {
                loop {
                    #[cfg(feature = "diagnostics")]
                    stack_scan.step();
                    let now = Instant::now().as_micros();
                    if let Some(reply) = service.tick(&state, now) {
                        state.borrow_mut().reset_pending = matches!(
                            reply.effect,
                            pumper_core::controller::Effect::Restart
                                | pumper_core::controller::Effect::Bootsel
                        );
                        // Give USB/audio a turn after tick() before another spectrum slice.
                        yield_now().await;
                        let sent = hid_progress::complete(hid_in.write(&reply.bytes), async {
                            loop {
                                if service.maintain_pending(&state, Instant::now().as_micros()) {
                                    yield_now().await;
                                } else {
                                    Timer::after(Duration::from_micros(250)).await;
                                }
                            }
                        })
                        .await;
                        if sent.is_ok() {
                            state
                                .borrow_mut()
                                .activity
                                .notify(Instant::now().as_micros() as u32);
                            match reply.effect {
                                pumper_core::controller::Effect::Restart => {
                                    Timer::after(Duration::from_millis(250)).await;
                                    hardware::restart();
                                }
                                pumper_core::controller::Effect::Bootsel => {
                                    Timer::after(Duration::from_millis(250)).await;
                                    embassy_rp::rom_data::reset_to_usb_boot(0, 0);
                                }
                                _ => {}
                            }
                        }
                        state.borrow_mut().reset_pending = false;
                    }
                    let spectrum_active = state.borrow().controller.spectrum_deadline != 0;
                    if spectrum_active {
                        yield_now().await;
                    } else {
                        Timer::after(Duration::from_micros(250)).await;
                    }
                }
            },
            async {
                loop {
                    let mut sum = 0u32;
                    for _ in 0..8 {
                        sum += u32::from(adc.read(&mut temperature).await.unwrap_or(876));
                    }
                    let volts = (sum as f32 / 8.0) * (3.3 / 4096.0);
                    let milli =
                        libm::roundevenf((27.0 - (volts - 0.706) / 0.001721) * 1000.0) as i32;
                    SHARED.temperature.store(milli as u32, Ordering::Relaxed);
                    Timer::after(Duration::from_millis(500)).await;
                }
            },
            led::run(
                || async {
                    Timer::after(Duration::from_millis(1)).await;
                    Instant::now().as_micros() as u32
                },
                |now| {
                    let mut s = state.borrow_mut();
                    let streaming = s.stream.alternate != 0;
                    let (red, blue) = led::levels(
                        &mut s.activity,
                        now,
                        streaming,
                        SHARED.peak.load(Ordering::Relaxed),
                    );
                    hardware::leds(red, blue);
                },
            ),
        ),
    ));
    unreachable!()
}

#[panic_handler]
fn panic(_: &core::panic::PanicInfo<'_>) -> ! {
    // Firmware faults must be recoverable without another firmware.
    embassy_rp::rom_data::reset_to_usb_boot(0, 0);
    loop {
        cortex_m::asm::wfe();
    }
}

#[cortex_m_rt::exception]
unsafe fn HardFault(_: &cortex_m_rt::ExceptionFrame) -> ! {
    embassy_rp::rom_data::reset_to_usb_boot(0, 0);
    loop {
        cortex_m::asm::wfe();
    }
}
