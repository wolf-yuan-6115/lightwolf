#[path = "../../src/hid_progress.rs"]
mod hid_progress;
#[path = "../../src/led.rs"]
mod led;

use pumper_core::activity::Activity;
use std::{
    cell::{Cell, RefCell},
    future::{Future, poll_fn},
    pin::pin,
    task::{Context, Poll, Waker},
};

#[test]
fn leds_keep_refreshing_when_controller_stops_reading_hid() {
    let now = Cell::new(0u32);
    let tick_ready = Cell::new(false);
    let streaming = Cell::new(true);
    let peak = Cell::new(1000);
    let updates = RefCell::new(Vec::new());
    let mut activity = Activity::default();
    activity.notify(0);
    activity.notify(1); // coalesced activity must finish without another report
    let mut leds = pin!(led::run(
        || poll_fn(|_| {
            if tick_ready.replace(false) {
                Poll::Ready(now.get())
            } else {
                Poll::Pending
            }
        }),
        |time| updates.borrow_mut().push(led::levels(
            &mut activity,
            time,
            streaming.get(),
            peak.get(),
        )),
    ));
    let acknowledged = Cell::new(false);
    let mut hid = pin!(hid_progress::complete(
        poll_fn(|_| if acknowledged.get() {
            Poll::Ready(())
        } else {
            Poll::Pending
        }),
        std::future::pending(), // no background work: LEDs still own their timer
    ));
    let mut cx = Context::from_waker(Waker::noop());
    for (time, level, red) in [
        (1000, 1000, false),
        (25000, 2000, true),
        (50000, 3000, false),
        (75000, 4000, true),
        (100000, 0, true),
    ] {
        now.set(time);
        peak.set(level);
        tick_ready.set(true);
        assert!(hid.as_mut().poll(&mut cx).is_pending());
        assert!(leds.as_mut().poll(&mut cx).is_pending());
        assert_eq!(updates.borrow().last(), Some(&(red, level)));
        let count = updates.borrow().len();
        assert!(leds.as_mut().poll(&mut cx).is_pending());
        assert_eq!(updates.borrow().len(), count); // no timer tick, no busy loop
    }
    // USB audio stopping must clear both LEDs, even if the last peak was loud.
    streaming.set(false);
    peak.set(32768);
    tick_ready.set(true);
    assert!(leds.as_mut().poll(&mut cx).is_pending());
    assert_eq!(updates.borrow().last(), Some(&(false, 0)));
    // The original IN transfer can still complete after the controller returns.
    acknowledged.set(true);
    assert!(hid.as_mut().poll(&mut cx).is_ready());
}

#[test]
fn idle_clears_pending_activity_and_blue_peak_across_timer_wrap() {
    let mut activity = Activity::default();
    let start = u32::MAX - 1000;
    activity.notify(start);
    assert_eq!(
        led::levels(&mut activity, start, true, 12000),
        (false, 12000)
    );
    assert_eq!(
        led::levels(&mut activity, start.wrapping_add(25000), true, 8000),
        (true, 8000)
    );
    assert_eq!(led::levels(&mut activity, 30000, false, 8000), (false, 0));
    assert_eq!(led::levels(&mut activity, 31000, true, 0), (true, 0));
}
