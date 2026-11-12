//! Verification : demarre un .fwsc, joue des notes, mesure la vitesse, le son et l'ecran.
use std::time::Instant;
fn main() {
    let path = std::env::args().nth(1).expect("usage: fm1hw-check paquet.fwsc [secondes]");
    let secs: f64 = std::env::args().nth(2).and_then(|s| s.parse().ok()).unwrap_or(4.0);
    let pkg = std::fs::read(&path).expect("lecture");
    let mut err = [0i8; 256];
    let mhz: u32 = std::env::args().nth(3).and_then(|s| s.parse().ok()).unwrap_or(240);
    let m = fm1hw::fm1hw_boot(pkg.as_ptr(), pkg.len(), mhz, err.as_mut_ptr(), 256);
    if m.is_null() { println!("ECHEC demarrage: {}", unsafe { std::ffi::CStr::from_ptr(err.as_ptr()) }.to_string_lossy()); std::process::exit(1); }
    fm1hw::fm1hw_master(m, 900);
    let mut buf = vec![0i16; 65536];
    let dump = std::env::var("FM1_WAV").ok();                 // ecrit le son produit (wav 44,1 kHz stereo)
    let mut all: Vec<i16> = Vec::new();
    let (t0, mut frames, mut peak, mut step) = (Instant::now(), 0u64, 0i32, 0);
    while (frames as f64) < secs * 44100.0 {
        if t0.elapsed().as_secs_f64() > 20.0 { println!("ARRET: pas de son apres 20 s (firmware bloque ou trop lent)"); std::process::exit(2); }
        let n = fm1hw::fm1hw_run(m, 20000, buf.as_mut_ptr(), buf.len() / 2);
        if n < 0 { let mut e = [0i8; 256]; fm1hw::fm1hw_error(m, e.as_mut_ptr(), 256); println!("ARRET: {}", unsafe { std::ffi::CStr::from_ptr(e.as_ptr()) }.to_string_lossy()); break; }
        if frames > 44100 { for s in &buf[..n as usize * 2] { peak = peak.max((*s as i32).abs()); } }
        if dump.is_some() { all.extend_from_slice(&buf[..n as usize * 2]); }
        if std::env::var("FM1_AT").is_ok() && frames > 4 * 22050 { for b in [0xD0u8, ((frames / 300) % 100) as u8] { fm1hw::fm1hw_midi(m, b); } }
        frames += n as u64;
        // a la 1re seconde : PLAY ; puis des notes (DO4 = id 21)
        let t = frames / 22050;
        if t != step { step = t;
            if t == 2 && std::env::var("FM1_NOPLAY").is_err() { fm1hw::fm1hw_key(m, 12, 1, 0); }
            if t == 3 { fm1hw::fm1hw_key(m, 12, 0, 0); }
            if t >= 4 && std::env::var("FM1_MIDI").is_err() && std::env::var("FM1_NONE").is_err() { fm1hw::fm1hw_key(m, 21 + (t as i32 % 5), (t % 2 == 0) as i32, 100); }
            if t >= 4 && std::env::var("FM1_MIDI").is_ok() {
                // notes par le MIDI (comme un clavier maitre), plus un flux d'aftertouch
                let n = 60 + (t as u8 % 5) * 2;
                if t % 2 == 0 { for b in [0x90u8, n, 100] { fm1hw::fm1hw_midi(m, b); } } else { for b in [0x80u8, n - 2, 0] { fm1hw::fm1hw_midi(m, b); } }
            }
        }
    }
    let wall = t0.elapsed().as_secs_f64();
    let lcd = unsafe { std::slice::from_raw_parts(fm1hw::fm1hw_lcd(m), 240 * 240) };
    let lit = lcd.iter().filter(|&&p| p != 0).count();
    let mut leds = [0f32; 41]; fm1hw::fm1hw_leds(m, leds.as_mut_ptr());
    println!("OK {} : {:.1} s emulees en {:.2} s ({:.1}x temps reel), crete {} ({:.3}), ecran {} px allumes, LED {}",
        path.rsplit('/').next().unwrap(), frames as f64 / 44100.0, wall, frames as f64 / 44100.0 / wall, peak, peak as f64 / 32768.0, lit,
        leds.iter().filter(|&&l| l > 0.01).count());
    if let Some(p) = dump {
        let mut w: Vec<u8> = Vec::new();
        let data = all.len() as u32 * 2;
        w.extend_from_slice(b"RIFF"); w.extend_from_slice(&(36 + data).to_le_bytes()); w.extend_from_slice(b"WAVEfmt ");
        w.extend_from_slice(&16u32.to_le_bytes()); w.extend_from_slice(&1u16.to_le_bytes()); w.extend_from_slice(&2u16.to_le_bytes());
        w.extend_from_slice(&44100u32.to_le_bytes()); w.extend_from_slice(&(44100u32 * 4).to_le_bytes()); w.extend_from_slice(&4u16.to_le_bytes()); w.extend_from_slice(&16u16.to_le_bytes());
        w.extend_from_slice(b"data"); w.extend_from_slice(&data.to_le_bytes());
        for s in &all { w.extend_from_slice(&s.to_le_bytes()); }
        std::fs::write(p, w).unwrap();
    }
    fm1hw::fm1hw_free(m);
}
