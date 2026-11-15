use crate::shared::SHARED;
use core::sync::atomic::Ordering;
use heapless::spsc::{Consumer, Producer};
use pumper_core::format::{self, Format, MAX_FRAMES};

#[repr(align(16))]
pub struct Block {
    pub samples: [f32; MAX_FRAMES * 2],
    pub words: [u32; MAX_FRAMES * 2],
    pub frames: usize,
    pub word_count: usize,
    pub generation: u32,
    pub format: Format,
}
impl Block {
    pub const fn new() -> Self {
        Self {
            samples: [0.0; MAX_FRAMES * 2],
            words: [0; MAX_FRAMES * 2],
            frames: 0,
            word_count: 0,
            generation: 0,
            format: Format::Pcm16,
        }
    }
}
pub type Queue = heapless::spsc::Queue<&'static mut Block, 8>;
pub type Sender = Producer<'static, &'static mut Block>;
pub type Receiver = Consumer<'static, &'static mut Block>;

pub type ConfigQueue = heapless::spsc::Queue<pumper_core::config::Runtime, 4>;
#[derive(Clone, Copy)]
pub struct MeterBlock {
    pub epoch: u32,
    pub metrics: pumper_core::dsp::Metrics,
}
pub type MeterQueue = heapless::spsc::Queue<MeterBlock, 128>;

pub fn core1(
    mut ready: Receiver,
    mut free: Sender,
    mut config_rx: heapless::spsc::Consumer<'static, pumper_core::config::Runtime>,
    mut meter_tx: heapless::spsc::Producer<'static, MeterBlock>,
    flash_channels: (crate::flash::Receiver, crate::flash::Sender),
    mut capture: pumper_core::spectrum::Producer<'static>,
    mut runtime: pumper_core::config::Runtime,
) -> ! {
    let (mut flash_rx, mut flash_tx) = flash_channels;
    #[cfg(feature = "diagnostics")]
    let mut stack_scan = crate::diagnostics::StackScan::new(true);
    let mut output = crate::i2s::I2s::new();
    let mut stream = SHARED.stream();
    let mut dsp = pumper_core::dsp::Dsp::new(stream.rate, runtime);
    let mut stats = crate::telemetry::AudioStats::default();
    let mut pending_meter = MeterBlock {
        epoch: 0,
        metrics: Default::default(),
    };
    output.configure(stream.rate, stream.format, false, &mut free);
    SHARED.applied.store(stream.generation, Ordering::Release);
    SHARED
        .eq_applied
        .store(runtime.generation, Ordering::Release);
    loop {
        if let Some(job) = flash_rx.dequeue() {
            output.flash_mute(&mut free);
            while let Some(block) = ready.dequeue() {
                crate::shared::owned(free.enqueue(block));
            }
            crate::flash::execute(job);
            flash_tx
                .enqueue(job)
                .ok()
                .expect("one flash operation in flight");
        }
        if SHARED.flash_busy.load(Ordering::Acquire) {
            continue;
        }
        let next = SHARED.stream();
        if next.generation != stream.generation {
            if next.format.accepts(next.rate) {
                output.configure(next.rate, next.format, next.alternate != 0, &mut free);
            } else {
                output.configure(stream.rate, stream.format, false, &mut free);
            }
            dsp.set_rate(next.rate);
            dsp.reset_stream();
            stream = next;
            SHARED.applied.store(stream.generation, Ordering::Release);
        }
        let total_started = embassy_time::Instant::now().as_micros();
        let mut latest = None;
        while let Some(c) = config_rx.dequeue() {
            latest = Some(c);
        }
        if let Some(c) = latest {
            if c.generation != runtime.generation && dsp.set_config(c.eq) {
                SHARED.eq_applied.store(c.generation, Ordering::Release);
            }
            if c.controls_generation != runtime.controls_generation {
                dsp.controls.configure(c.crossfeed, c.output, c.gains);
            }
            runtime = c;
        }
        output.service(&mut free);
        if let Some(block) = ready.dequeue() {
            if block.generation != stream.generation || stream.alternate == 0 {
                crate::shared::owned(free.enqueue(block));
                continue;
            }
            let measure = SHARED.meter_enabled.load(Ordering::Acquire);
            let meter_epoch = SHARED.meter_epoch.load(Ordering::Acquire);
            capture.begin(stream.rate, stream.generation);
            let started = embassy_time::Instant::now().as_micros();
            let metrics = dsp.process(&mut block.samples[..block.frames * 2], measure, |pair| {
                capture.push(pair)
            });
            let elapsed = (embassy_time::Instant::now().as_micros() - started) as u32;
            capture.end(elapsed);
            stats.block(
                stream.generation,
                block.frames as u32,
                elapsed,
                metrics.limiter_frames,
            );
            crate::telemetry::AUDIO.publish(stats);
            SHARED.dsp_us.fetch_max(elapsed, Ordering::Relaxed);
            SHARED.peak.store(
                u32::from(metrics.peaks[2].max(metrics.peaks[3])),
                Ordering::Relaxed,
            );
            if measure {
                if pending_meter.epoch != meter_epoch {
                    pending_meter = MeterBlock {
                        epoch: meter_epoch,
                        metrics: Default::default(),
                    };
                }
                pending_meter.metrics.merge(metrics);
                if meter_tx.enqueue(pending_meter).is_ok() {
                    pending_meter.metrics = Default::default();
                }
            }
            block.word_count = format::pack(
                &mut block.words,
                &block.samples[..block.frames * 2],
                block.format,
            )
            .expect("validated audio block");
            output.submit(block);
            #[cfg(feature = "diagnostics")]
            stack_scan.step();
            SHARED.block_us.fetch_max(
                (embassy_time::Instant::now().as_micros() - total_started) as u32,
                Ordering::Relaxed,
            );
        }
    }
}
