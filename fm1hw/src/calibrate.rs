//! Calibrage d'un .fwsc (outil du catalogue) : "calib <mhz_min> <masque des canaux>".
fn main() {
    let path = std::env::args().nth(1).expect("usage: fm1hw-calibrate paquet.fwsc");
    let pkg = std::fs::read(&path).expect("lecture");
    let (mut mhz, mut ch) = (0u32, 0u32);
    let t = std::time::Instant::now();
    let ok = fm1hw::fm1hw_calibrate(pkg.as_ptr(), pkg.len(), &mut mhz, &mut ch);
    if ok == 1 { println!("calib {} {} ({:.1} s)", mhz, ch, t.elapsed().as_secs_f64()); } else { println!("calib fail"); }
}
