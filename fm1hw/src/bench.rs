//! Banc de mesure des craquements : pour un .fwsc et une horloge, joue des accords en MIDI et mesure
//! - speed : temps emule / temps de calcul (au-dessous de 1, l'emulateur ne suit pas le temps reel) ;
//! - lead  : avance minimale (ms) qu'il faut pour ne jamais manquer de son sur cette machine ;
//! - late  : demi-buffers rejoues par le DAC (le firmware n'a pas rempli a temps : il manque d'horloge) ;
//! - skip  : part des cycles sautes dans les boucles d'attente.
use std::time::Instant;
fn main() {
    let a: Vec<String> = std::env::args().collect();
    let pkg = std::fs::read(&a[1]).expect("lecture");
    let mhz: u32 = a.get(2).and_then(|s| s.parse().ok()).unwrap_or(240);
    let secs: f64 = a.get(3).and_then(|s| s.parse().ok()).unwrap_or(8.0);
    let mut err = [0i8; 256];
    let m = fm1hw::fm1hw_boot(pkg.as_ptr(), pkg.len(), mhz, err.as_mut_ptr(), 256);
    if m.is_null() { println!("BOOTFAIL"); return; }
    fm1hw::fm1hw_master(m, 900);
    let mut buf = vec![0i16; 65536];
    let mut audio: Vec<i16> = Vec::new();
    let (h0, c0, _) = fm1hw::stats(m);
    let warm = 3.0 * 44100.0;
    let (mut frames, mut step) = (0u64, 0u64);
    let mut t_meas: Option<Instant> = None;
    let (mut d_min, mut lead, mut wall_meas) = (0f64, 0f64, 0f64);
    let (mut hm0, mut cm0, mut sm0) = (0, 0, 0);
    let mut audio_from = 0usize;
    let t0 = Instant::now();
    while (frames as f64) < warm + secs * 44100.0 {
        if t0.elapsed().as_secs_f64() > 120.0 { println!("TIMEOUT"); return; }
        let ts = Instant::now();
        let n = fm1hw::fm1hw_run(m, 20000, buf.as_mut_ptr(), buf.len() / 2);
        let dt = ts.elapsed().as_secs_f64();
        if n < 0 { println!("HALT"); return; }
        audio.extend_from_slice(&buf[..n as usize * 2]);
        frames += n as u64;
        if (frames as f64) >= warm {
            if t_meas.is_none() { t_meas = Some(Instant::now()); let s = fm1hw::stats(m); hm0 = s.0; cm0 = s.1; sm0 = s.2; audio_from = audio.len(); d_min = 0.0; }
            else {
                wall_meas += dt;
                // D = temps de calcul - temps audio produit, depuis le debut de la mesure
                let d = wall_meas - ((audio.len() - audio_from) / 2) as f64 / 44100.0;
                d_min = d_min.min(d);
                lead = lead.max(d - d_min);
            }
        }
        // accords MIDI : 4 notes, renouveles toutes les 0,25 s ; PLAY a 1 s
        let t = frames / 11025;
        if t != step {
            step = t;
            if t == 4 { fm1hw::fm1hw_key(m, 12, 1, 0); }
            if t == 5 { fm1hw::fm1hw_key(m, 12, 0, 0); }
            if t >= 8 {
                let base = 48 + ((t / 4) % 4) as u8 * 3;
                for k in 0..4u8 { for b in [0x80u8, base.wrapping_sub(3) + k * 4, 0] { fm1hw::fm1hw_midi(m, b); } }
                for k in 0..4u8 { for b in [0x90u8, base + k * 4, 100] { fm1hw::fm1hw_midi(m, b); } }
            }
        }
    }
    let (h1, c1, s1) = fm1hw::stats(m);
    let halves = (h1 - hm0).max(1);
    let meas = &audio[audio_from..];
    let per = (meas.len() as u64 / halves) as usize;      // echantillons par demi-buffer
    let mut late = 0;
    if per >= 64 {
        let hs: Vec<&[i16]> = meas.chunks_exact(per).collect();
        for k in 2..hs.len() {
            let loud = hs[k].iter().any(|v| v.unsigned_abs() > 64);
            if loud && hs[k] == hs[k - 2] { late += 1; }
        }
    }
    let emu = ((audio.len() - audio_from) / 2) as f64 / 44100.0;
    let peak = meas.iter().map(|v| v.unsigned_abs()).max().unwrap_or(0);
    let _ = (h0, c0);
    println!("speed {:.2} lead {:.0}ms late {}/{} half {} skip {:.0}% peak {}", emu / wall_meas.max(1e-9), lead * 1000.0, late, halves, per / 2,
             100.0 * (s1 - sm0) as f64 / (c1 - cm0).max(1) as f64, peak);
}
