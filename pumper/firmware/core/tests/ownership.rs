//! Abstract interleaving model of the device's six-token pipeline. This checks
//! policy conservation and abort ordering, not peripheral register semantics.
use std::collections::VecDeque;

#[derive(Debug)]
struct Token {
    id: usize,
    epoch: u32,
}
struct Model {
    free: VecDeque<Token>,
    usb: Option<Token>,
    ready: VecDeque<Token>,
    dsp: Option<Token>,
    pending: VecDeque<Token>,
    dma: [Option<Token>; 2],
    requested: u32,
    applied: u32,
    aborting: bool,
    streaming: bool,
}
impl Model {
    fn new() -> Self {
        Self {
            free: (0..6).map(|id| Token { id, epoch: 0 }).collect(),
            usb: None,
            ready: VecDeque::new(),
            dsp: None,
            pending: VecDeque::new(),
            dma: [None, None],
            requested: 0,
            applied: 0,
            aborting: false,
            streaming: true,
        }
    }
    fn check(&self) {
        let mut owners = [0; 6];
        for token in self
            .free
            .iter()
            .chain(self.usb.iter())
            .chain(self.ready.iter())
            .chain(self.dsp.iter())
            .chain(self.pending.iter())
            .chain(self.dma.iter().flatten())
        {
            owners[token.id] += 1;
        }
        assert_eq!(
            owners, [1; 6],
            "token lost or reused while another owner holds it"
        );
        assert!(self.free.len() <= 6 && self.ready.len() <= 6 && self.pending.len() <= 6);
    }
    fn step(&mut self, action: usize) {
        match action {
            0 if self.usb.is_none() => {
                self.usb = self.free.pop_front().map(|mut t| {
                    t.epoch = self.requested;
                    t
                });
            }
            1 => {
                if let Some(t) = self.usb.take() {
                    self.ready.push_back(t);
                }
            }
            2 if self.dsp.is_none() && !self.aborting && self.applied == self.requested => {
                if let Some(t) = self.ready.pop_front() {
                    if self.streaming && t.epoch == self.applied {
                        self.dsp = Some(t);
                    } else {
                        self.free.push_back(t);
                    }
                }
            }
            3 => {
                if let Some(t) = self.dsp.take() {
                    self.pending.push_back(t);
                }
            }
            4 | 5 if !self.aborting && self.applied == self.requested => {
                let slot = action - 4;
                if self.dma[slot].is_none() {
                    self.dma[slot] = self.pending.pop_front();
                }
            }
            6 | 7 if !self.aborting => {
                if let Some(t) = self.dma[action - 6].take() {
                    self.free.push_back(t);
                }
            }
            8 => {
                // disconnect, format/rate change or flash quiescence requested
                self.requested = self.requested.wrapping_add(1);
                self.streaming = !self.streaming;
            }
            9 if self.dsp.is_none() && self.applied != self.requested => {
                // Starting abort must not return the DMA-owned tokens.
                let held = self.dma.each_ref().map(|t| t.as_ref().map(|t| t.id));
                self.aborting = true;
                assert_eq!(held, self.dma.each_ref().map(|t| t.as_ref().map(|t| t.id)));
            }
            10 if self.aborting => {
                // completed hardware abort acknowledges ownership
                self.free
                    .extend(self.dma.iter_mut().filter_map(Option::take));
                self.free.extend(self.pending.drain(..));
                self.applied = self.requested;
                self.aborting = false;
            }
            _ => {}
        }
        self.check();
    }
}

#[test]
fn full_queues_reset_and_completed_abort_conserve_six_tokens() {
    let mut m = Model::new();
    for _ in 0..6 {
        m.step(0);
        m.step(1);
    }
    assert_eq!(m.ready.len(), 6);
    m.step(0);
    assert!(
        m.usb.is_none(),
        "full pool applies backpressure without inventing a block"
    );
    for i in 0..2 {
        m.step(2);
        m.step(3);
        m.step(4 + i);
    }
    assert!(m.dma.iter().all(Option::is_some));
    m.step(8);
    m.step(9);
    assert!(m.free.is_empty());
    m.step(6);
    m.step(7); // no completion allowed before the abort acknowledgement
    assert!(m.free.is_empty());
    m.step(10);
    assert_eq!(m.free.len(), 2);
    for _ in 0..4 {
        m.step(2);
    }
    assert_eq!(m.free.len(), 6);

    let mut seed = 0x8f05_2354_u32;
    let mut visits = [0u32; 11];
    for _ in 0..200_000 {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        let action = seed as usize % 11;
        visits[action] += 1;
        m.step(action);
    }
    assert!(visits.iter().all(|v| *v > 10_000));
}
