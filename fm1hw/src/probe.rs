//! Sonde du canal MIDI : pour chaque canal 1-16, une note et l'energie du son qu'elle produit
//! (par rapport au silence). Repond sur tous les canaux = omni ; sur un seul ; ou sur plusieurs (multitimbral).
fn main() {
    let a: Vec<String> = std::env::args().collect();
    let pkg = std::fs::read(&a[1]).expect("lecture");
    let mut err = [0i8; 256];
    let m = fm1hw::fm1hw_boot(pkg.as_ptr(), pkg.len(), 200, err.as_mut_ptr(), 256);
    if m.is_null() { println!("BOOTFAIL"); return; }
    fm1hw::fm1hw_master(m, 900);
    let mut buf = vec![0i16; 65536];
    let mut run = |frames: u64| -> f64 {
        let (mut got, mut e) = (0u64, 0f64);
        while got < frames {
            let n = fm1hw::fm1hw_run(m, 20000, buf.as_mut_ptr(), buf.len() / 2);
            if n < 0 { return -1.0; }
            for s in &buf[..n as usize * 2] { e += (*s as f64) * (*s as f64); }
            got += n as u64;
        }
        (e / (2 * got.max(1)) as f64).sqrt()
    };
    run(44100 * 3);
    let base = run(13230);
    let mut out = vec![];
    for ch in 0..16u8 {
        for b in [0x90 | ch, 64, 110] { fm1hw::fm1hw_midi(m, b); }
        let on = run(13230);
        for b in [0x80 | ch, 64, 0] { fm1hw::fm1hw_midi(m, b); }
        run(22050);
        out.push(on);
    }
    let mx = out.iter().cloned().fold(0.0, f64::max);
    let hit: Vec<usize> = (0..16).filter(|&k| out[k] > base * 2.0 + 30.0 && out[k] > mx * 0.15).map(|k| k + 1).collect();
    println!("base {:.0} reponses {:?} -> canaux {:?}", base, out.iter().map(|v| *v as i64).collect::<Vec<_>>(), hit);
}
