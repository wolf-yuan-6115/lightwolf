#![no_std]
#![forbid(unsafe_code)]

/// Firmware release reported through HID; wire protocol remains version 1.
pub const FIRMWARE_VERSION: (u8, u8) = (5, 0);

pub mod activity;
pub mod config;
pub mod controller;
pub mod controls;
pub mod descriptors;
pub mod dsp;
pub mod feedback;
pub mod format;
pub mod protocol;
pub mod spectrum;
mod spectrum_tables;
pub mod storage;
pub mod usb;
