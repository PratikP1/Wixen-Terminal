//! Benchmarks for terminal grid operations.
//!
//! Tests the hot paths that run on every frame: print, scroll, CSI dispatch,
//! visible_text extraction, and full pipeline (parser → terminal).

use criterion::{Criterion, Throughput, black_box, criterion_group, criterion_main};
use wixen_core::Terminal;
use wixen_vt::{Action, Parser};

fn dispatch_action(terminal: &mut Terminal, action: Action) {
    match action {
        Action::Print(ch) => terminal.print(ch),
        Action::Execute(byte) => terminal.execute(byte),
        Action::CsiDispatch {
            params,
            subparams,
            intermediates,
            action,
        } => terminal.csi_dispatch(&params, &intermediates, action, &subparams),
        Action::EscDispatch {
            intermediates,
            action,
        } => terminal.esc_dispatch(&intermediates, action),
        Action::OscDispatch(params) => terminal.osc_dispatch(&params),
        Action::DcsHook {
            params,
            intermediates,
            action,
        } => terminal.dcs_hook(&params, &intermediates, action),
        Action::DcsPut(byte) => terminal.dcs_put(byte),
        Action::DcsUnhook => terminal.dcs_unhook(),
        Action::ApcDispatch(data) => terminal.apc_dispatch(&data),
    }
}

fn feed(terminal: &mut Terminal, parser: &mut Parser, data: &[u8]) {
    let mut actions = Vec::new();
    for &byte in data {
        parser.advance(byte, &mut actions);
    }
    for action in actions {
        dispatch_action(terminal, action);
    }
}

fn bench_grid(c: &mut Criterion) {
    // --- Print throughput ---
    let mut group = c.benchmark_group("terminal/print");
    for &cols in &[80, 160, 320] {
        let text: String = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
            .chars()
            .cycle()
            .take(cols * 24)
            .collect();
        group.throughput(Throughput::Elements((cols * 24) as u64));
        group.bench_with_input(format!("{cols}x24"), &text, |b, text| {
            b.iter(|| {
                let mut terminal = Terminal::new(cols, 24);
                for ch in text.chars() {
                    terminal.print(ch);
                }
                black_box(terminal.visible_text().len());
            });
        });
    }
    group.finish();

    // --- Full pipeline throughput ---
    let mut group = c.benchmark_group("terminal/pipeline");
    for &size in &[1024, 16 * 1024, 64 * 1024] {
        // Colored output with newlines
        let mut data = Vec::with_capacity(size);
        let line = b"\x1b[32mOK\x1b[0m: some status message line here\r\n";
        while data.len() < size {
            data.extend_from_slice(line);
        }
        data.truncate(size);

        group.throughput(Throughput::Bytes(size as u64));
        group.bench_with_input(
            format!("{:.0}KB", size as f64 / 1024.0),
            &data,
            |b, data| {
                b.iter(|| {
                    let mut terminal = Terminal::new(120, 40);
                    let mut parser = Parser::new();
                    feed(&mut terminal, &mut parser, data);
                    black_box(terminal.visible_text().len());
                });
            },
        );
    }
    group.finish();

    // --- Scroll-heavy workload ---
    c.bench_function("terminal/scroll_heavy", |b| {
        // 10,000 lines through a 24-row terminal
        let mut data = Vec::new();
        for i in 0..10_000u32 {
            data.extend_from_slice(format!("Line {i:05}: some output text\r\n").as_bytes());
        }
        b.iter(|| {
            let mut terminal = Terminal::new(80, 24);
            let mut parser = Parser::new();
            feed(&mut terminal, &mut parser, &data);
            black_box(terminal.scrollback.len());
        });
    });

    // --- visible_text extraction ---
    c.bench_function("terminal/visible_text", |b| {
        let mut terminal = Terminal::new(120, 40);
        let mut parser = Parser::new();
        // Fill screen with content
        let mut data = Vec::new();
        for i in 0..40u32 {
            data.extend_from_slice(
                format!("\x1b[33mLine {i:03}\x1b[0m: the quick brown fox jumps\r\n").as_bytes(),
            );
        }
        feed(&mut terminal, &mut parser, &data);

        b.iter(|| {
            black_box(terminal.visible_text());
        });
    });

    // --- Resize ---
    c.bench_function("terminal/resize", |b| {
        let mut terminal = Terminal::new(80, 24);
        let mut parser = Parser::new();
        let mut data = Vec::new();
        for i in 0..100u32 {
            data.extend_from_slice(format!("Line {i}: content here\r\n").as_bytes());
        }
        feed(&mut terminal, &mut parser, &data);

        b.iter(|| {
            terminal.resize(120, 40);
            terminal.resize(80, 24);
            black_box(terminal.cols());
        });
    });

    // --- Keypress-to-grid latency (single character input processing) ---
    c.bench_function("terminal/keypress_latency", |b| {
        let mut terminal = Terminal::new(120, 40);
        let mut parser = Parser::new();
        // Fill terminal with some content first
        let mut setup = Vec::new();
        for i in 0..30u32 {
            setup.extend_from_slice(format!("Line {i}: existing content\r\n").as_bytes());
        }
        feed(&mut terminal, &mut parser, &setup);

        b.iter(|| {
            // Simulate a single keypress: parse + dispatch + grid update
            // This is the latency a user perceives per keystroke
            let mut actions = Vec::new();
            parser.advance(b'x', &mut actions);
            for action in actions {
                dispatch_action(&mut terminal, action);
            }
            black_box(terminal.grid.cursor.col);
        });
    });

    // --- Frame preparation: extract visible text for rendering ---
    c.bench_function("terminal/frame_prep_visible_text", |b| {
        let mut terminal = Terminal::new(120, 40);
        let mut parser = Parser::new();
        // Fill with realistic colored output
        let mut data = Vec::new();
        for i in 0..40u32 {
            data.extend_from_slice(
                format!("\x1b[38;2;100;200;{i}mLine {i:03}\x1b[0m: build output status message here with some detail\r\n")
                    .as_bytes(),
            );
        }
        feed(&mut terminal, &mut parser, &data);

        b.iter(|| {
            black_box(terminal.visible_text());
        });
    });

    // --- A11y tree data extraction (shell integration blocks) ---
    c.bench_function("terminal/a11y_data_extraction", |b| {
        let mut terminal = Terminal::new(120, 40);
        let mut parser = Parser::new();
        // Simulate 10 command blocks via OSC 133
        for i in 0..10u32 {
            let cmd = format!(
                "\x1b]133;A\x07$ command_{i}\x1b]133;B\x07\x1b]133;C\x07output line 1\r\noutput line 2\r\n\x1b]133;D;0\x07"
            );
            feed(&mut terminal, &mut parser, cmd.as_bytes());
        }

        b.iter(|| {
            // Extract all shell integration blocks (used to rebuild a11y tree)
            let blocks = terminal.shell_integration.blocks();
            black_box(blocks.len());
        });
    });
}

criterion_group!(benches, bench_grid);
criterion_main!(benches);
