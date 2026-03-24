//! Benchmarks for the scrollback buffer with zstd compression.
//!
//! Tests push throughput, compression trigger, and hot-tier access patterns.

use criterion::{Criterion, Throughput, black_box, criterion_group, criterion_main};
use wixen_core::ScrollbackBuffer;
use wixen_core::grid::Row;

/// Create a realistic terminal row (80 cells of mixed content).
fn make_row(cols: usize, seed: usize) -> Row {
    let mut row = Row::new(cols);
    for (i, cell) in row.cells.iter_mut().enumerate() {
        let ch = match (i + seed) % 4 {
            0 => 'A',
            1 => 'b',
            2 => ' ',
            _ => '.',
        };
        cell.content = ch.to_string();
    }
    row
}

fn bench_scrollback(c: &mut Criterion) {
    // --- Push throughput (no compression trigger) ---
    let mut group = c.benchmark_group("scrollback/push_hot");
    for &count in &[100, 1_000, 5_000] {
        let rows: Vec<Row> = (0..count).map(|i| make_row(80, i)).collect();
        group.throughput(Throughput::Elements(count as u64));
        group.bench_with_input(format!("{count}_rows"), &rows, |b, rows| {
            b.iter(|| {
                // Use a high threshold to avoid compression
                let mut buf = ScrollbackBuffer::with_threshold(count + 1000);
                for row in rows.iter() {
                    buf.push(row.clone());
                }
                black_box(buf.len());
            });
        });
    }
    group.finish();

    // --- Push throughput with compression triggers ---
    let mut group = c.benchmark_group("scrollback/push_with_compress");
    for &count in &[1_000, 5_000, 20_000] {
        let rows: Vec<Row> = (0..count).map(|i| make_row(80, i)).collect();
        group.throughput(Throughput::Elements(count as u64));
        // Low threshold forces frequent compression
        group.bench_with_input(format!("{count}_rows"), &rows, |b, rows| {
            b.iter(|| {
                let mut buf = ScrollbackBuffer::with_threshold(500);
                for row in rows.iter() {
                    buf.push(row.clone());
                }
                black_box(buf.len());
            });
        });
    }
    group.finish();

    // --- Hot-tier random access ---
    c.bench_function("scrollback/hot_access", |b| {
        let mut buf = ScrollbackBuffer::with_threshold(20_000);
        for i in 0..10_000 {
            buf.push(make_row(80, i));
        }
        b.iter(|| {
            // Access 100 random hot-tier rows
            let mut sum = 0usize;
            for i in (0..100).map(|x| x * 97 % 10_000) {
                if let Some(row) = buf.get(i) {
                    sum += row.cells.len();
                }
            }
            black_box(sum);
        });
    });
}

criterion_group!(benches, bench_scrollback);
criterion_main!(benches);
