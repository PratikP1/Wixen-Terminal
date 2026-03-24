//! Benchmarks for the VT parser hot path.
//!
//! The parser processes every byte from the PTY — it's the single
//! hottest code path in the terminal emulator.

use criterion::{Criterion, Throughput, black_box, criterion_group, criterion_main};
use wixen_vt::Parser;

/// Plain ASCII text — the common case for most terminal output.
fn gen_ascii(n: usize) -> Vec<u8> {
    let line = b"The quick brown fox jumps over the lazy dog. ";
    line.iter().cycle().take(n).copied().collect()
}

/// Mixed output: text + SGR color codes (simulates `ls --color` or compiler output).
fn gen_colored_output(n: usize) -> Vec<u8> {
    let mut buf = Vec::with_capacity(n);
    let colors = [
        b"\x1b[31m" as &[u8], // red
        b"\x1b[32m",          // green
        b"\x1b[33m",          // yellow
        b"\x1b[0m",           // reset
    ];
    let words = [b"error" as &[u8], b"warning", b"info", b"ok"];
    let mut i = 0;
    while buf.len() < n {
        buf.extend_from_slice(colors[i % 4]);
        buf.extend_from_slice(words[i % 4]);
        buf.push(b' ');
        i += 1;
    }
    buf.truncate(n);
    buf
}

/// Dense CSI — cursor movement, erase, scrolling (simulates a TUI app like htop).
fn gen_csi_heavy(n: usize) -> Vec<u8> {
    let mut buf = Vec::with_capacity(n);
    let sequences = [
        b"\x1b[H" as &[u8],     // cursor home
        b"\x1b[2J",             // erase display
        b"\x1b[10;20H",         // cursor position
        b"\x1b[Ksome text",     // erase line + text
        b"\x1b[1;32mOK\x1b[0m", // colored text
        b"\r\n",                // newline
    ];
    let mut i = 0;
    while buf.len() < n {
        buf.extend_from_slice(sequences[i % sequences.len()]);
        i += 1;
    }
    buf.truncate(n);
    buf
}

/// OSC-heavy — title changes, hyperlinks (simulates modern shell integration output).
fn gen_osc_heavy(n: usize) -> Vec<u8> {
    let mut buf = Vec::with_capacity(n);
    let sequences = [
        b"\x1b]0;window title\x07" as &[u8],
        b"\x1b]8;;https://example.com\x07link\x1b]8;;\x07",
        b"\x1b]133;A\x07PS> \x1b]133;B\x07",
    ];
    let mut i = 0;
    while buf.len() < n {
        buf.extend_from_slice(sequences[i % sequences.len()]);
        i += 1;
    }
    buf.truncate(n);
    buf
}

fn bench_parser(c: &mut Criterion) {
    let sizes = [1024, 16 * 1024, 256 * 1024];

    let mut group = c.benchmark_group("vt_parser/ascii");
    for &size in &sizes {
        let data = gen_ascii(size);
        group.throughput(Throughput::Bytes(size as u64));
        group.bench_with_input(
            format!("{:.0}KB", size as f64 / 1024.0),
            &data,
            |b, data| {
                b.iter(|| {
                    let mut parser = Parser::new();
                    let mut actions = Vec::new();
                    for &byte in data.iter() {
                        parser.advance(byte, &mut actions);
                    }
                    black_box(actions.len());
                });
            },
        );
    }
    group.finish();

    let mut group = c.benchmark_group("vt_parser/colored");
    for &size in &sizes {
        let data = gen_colored_output(size);
        group.throughput(Throughput::Bytes(size as u64));
        group.bench_with_input(
            format!("{:.0}KB", size as f64 / 1024.0),
            &data,
            |b, data| {
                b.iter(|| {
                    let mut parser = Parser::new();
                    let mut actions = Vec::new();
                    for &byte in data.iter() {
                        parser.advance(byte, &mut actions);
                    }
                    black_box(actions.len());
                });
            },
        );
    }
    group.finish();

    let mut group = c.benchmark_group("vt_parser/csi_heavy");
    for &size in &sizes {
        let data = gen_csi_heavy(size);
        group.throughput(Throughput::Bytes(size as u64));
        group.bench_with_input(
            format!("{:.0}KB", size as f64 / 1024.0),
            &data,
            |b, data| {
                b.iter(|| {
                    let mut parser = Parser::new();
                    let mut actions = Vec::new();
                    for &byte in data.iter() {
                        parser.advance(byte, &mut actions);
                    }
                    black_box(actions.len());
                });
            },
        );
    }
    group.finish();

    let mut group = c.benchmark_group("vt_parser/osc_heavy");
    for &size in &sizes {
        let data = gen_osc_heavy(size);
        group.throughput(Throughput::Bytes(size as u64));
        group.bench_with_input(
            format!("{:.0}KB", size as f64 / 1024.0),
            &data,
            |b, data| {
                b.iter(|| {
                    let mut parser = Parser::new();
                    let mut actions = Vec::new();
                    for &byte in data.iter() {
                        parser.advance(byte, &mut actions);
                    }
                    black_box(actions.len());
                });
            },
        );
    }
    group.finish();
}

criterion_group!(benches, bench_parser);
criterion_main!(benches);
