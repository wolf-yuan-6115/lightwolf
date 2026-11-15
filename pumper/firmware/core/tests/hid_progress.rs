#[path = "../../src/hid_progress.rs"]
mod hid_progress;
use std::{
    cell::Cell,
    future::{Future, poll_fn},
    pin::pin,
    task::{Context, Poll, Waker},
};

#[test]
fn pending_transfer_allows_work_and_completion_has_priority() {
    let ready = Cell::new(false);
    let slices = Cell::new(0);
    let polls = Cell::new(0);
    let transfer = poll_fn(|_| {
        polls.set(polls.get() + 1);
        if ready.get() {
            Poll::Ready(Ok::<_, ()>(64))
        } else {
            Poll::Pending
        }
    });
    let mut task = pin!(hid_progress::complete(
        transfer,
        poll_fn(|cx| {
            slices.set(slices.get() + 1);
            cx.waker().wake_by_ref();
            Poll::Pending
        })
    ));
    let mut cx = Context::from_waker(Waker::noop());
    for n in 1..=100 {
        assert!(task.as_mut().poll(&mut cx).is_pending());
        assert_eq!(slices.get(), n); // exactly one slice per executor poll
    }
    ready.set(true);
    assert_eq!(task.as_mut().poll(&mut cx), Poll::Ready(Ok(64)));
    assert_eq!(slices.get(), 100); // acknowledgement gets priority
    assert_eq!(polls.get(), 101); // original transfer stayed alive throughout
}

#[test]
fn disabled_transfer_returns_without_scheduling_work() {
    let mut task = pin!(hid_progress::complete(
        async { Err::<(), _>("disabled") },
        async { panic!("disabled endpoint must return immediately") }
    ));
    assert_eq!(
        task.as_mut().poll(&mut Context::from_waker(Waker::noop())),
        Poll::Ready(Err("disabled"))
    );
}

#[test]
fn stalled_hid_does_not_block_mute_publication_or_meter_expiry() {
    use pumper_core::{controller::Controller, storage::Store};
    use std::{
        cell::RefCell,
        sync::{
            Arc,
            atomic::{AtomicUsize, Ordering},
        },
        task::Wake,
    };
    struct WakeCount(AtomicUsize);
    impl Wake for WakeCount {
        fn wake(self: Arc<Self>) {
            self.0.fetch_add(1, Ordering::Relaxed);
        }
    }
    let c = RefCell::new(Controller::new(Store::default()));
    c.borrow_mut().meter.configured = true;
    c.borrow_mut().meter.deadline = 1250000;
    let now = Cell::new(0);
    let queue_full = Cell::new(true);
    let published = Cell::new(None);
    let timer_waker = RefCell::new(None::<Waker>);
    let timer_ready = Cell::new(false);
    let enabled = Cell::new(false);
    let transfer = std::future::pending::<()>();
    let background = async {
        loop {
            enabled.set(c.borrow_mut().maintain(now.get(), |runtime| {
                if queue_full.get() {
                    return false;
                }
                published.set(Some(runtime.gains));
                true
            }));
            poll_fn(|cx| {
                *timer_waker.borrow_mut() = Some(cx.waker().clone());
                if timer_ready.replace(false) {
                    Poll::Ready(())
                } else {
                    Poll::Pending
                }
            })
            .await;
        }
    };
    let mut task = pin!(hid_progress::complete(transfer, background));
    let wake_count = Arc::new(WakeCount(AtomicUsize::new(0)));
    let waker = Waker::from(wake_count.clone());
    let mut cx = Context::from_waker(&waker);
    assert!(task.as_mut().poll(&mut cx).is_pending());
    assert!(enabled.get());
    assert!(c.borrow().dirty_runtime); // a full queue must not drop the update
    c.borrow_mut().mute = true; // USB control request while HID remains unconsumed
    c.borrow_mut().publish_controls();
    queue_full.set(false);
    now.set(1250000);
    timer_ready.set(true);
    timer_waker.borrow().as_ref().unwrap().wake_by_ref();
    assert_eq!(wake_count.0.load(Ordering::Relaxed), 1);
    assert!(task.as_mut().poll(&mut cx).is_pending());
    assert_eq!(published.get(), Some([0.0; 2]));
    assert!(!c.borrow().dirty_runtime);
    assert!(!enabled.get());
    assert_eq!(c.borrow().meter.deadline, 0);
    assert_eq!(c.borrow().meter.epoch, 1);
    timer_ready.set(true);
    assert!(task.as_mut().poll(&mut cx).is_pending());
    assert_eq!(c.borrow().meter.epoch, 1); // expiry invalidates queued samples once
}
