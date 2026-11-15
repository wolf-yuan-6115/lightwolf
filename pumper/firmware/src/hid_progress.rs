//! Keep bounded background work moving without cancelling an in-flight HID write.
use core::{
    future::{Future, poll_fn},
    pin::pin,
    task::Poll,
};

pub async fn complete<F: Future>(transfer: F, background: impl Future<Output = ()>) -> F::Output {
    let mut transfer = pin!(transfer);
    let mut background = pin!(background);
    poll_fn(|cx| {
        // Completion has priority: acknowledgement and reboot handling must not
        // wait for another analysis slice. Keep this same future until it ends.
        if let Poll::Ready(result) = transfer.as_mut().poll(cx) {
            return Poll::Ready(result);
        }
        // Background work owns its timer/yield, so housekeeping still wakes
        // after spectrum expires and the host stops polling HID altogether.
        let _ = background.as_mut().poll(cx);
        Poll::Pending
    })
    .await
}
