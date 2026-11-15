//! LED refresh has its own clock and must never wait for HID IN completion.
use core::future::Future;

pub async fn run<T: Future<Output = u32>>(
    mut tick: impl FnMut() -> T,
    mut update: impl FnMut(u32),
) -> ! {
    loop {
        update(tick().await);
    }
}

pub fn levels(
    activity: &mut pumper_core::activity::Activity,
    now: u32,
    streaming: bool,
    peak: u32,
) -> (bool, u32) {
    (
        activity.level(now, streaming),
        if streaming { peak } else { 0 },
    )
}
