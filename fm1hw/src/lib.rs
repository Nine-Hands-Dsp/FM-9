//! FM-9 (NINE HANDS | DSP) : interface C de l'emulateur de la FM-1 (crates fm1-core / fm1-soc de
//! fm1-emu, Joakim Eriksson, licence MIT). Meme demarrage que le plugin de fm1-emu (engine.rs) :
//! le paquet .fwsc est decode, le chargeur de demarrage est simule, puis les deux coeurs pi32v2
//! tournent (compilateur JIT x86-64 / AArch64). La sortie audio du firmware est a 44,1 kHz stereo.
//!
//! Toutes les fonctions prennent une poignee rendue par fm1hw_boot ; un seul thread a la fois par
//! poignee (FM-9 les appelle depuis son thread d'emulation).

use fm1_core::{Bus, Cpu};
use fm1_soc::{machine, Action, Board, Soc};
use std::os::raw::{c_char, c_int};

const RAM_BASE: u32 = 0x01c0_0000;
const RAM_SIZE: usize = 1 << 20;
const SECTOR: usize = 4096;

pub struct Machine {
    cpu: Cpu,
    cpu1: Option<Cpu>,
    soc: Soc,
    pending_tick: bool,
    halted: bool,
    flash0: Vec<u8>,
    error: String,
}

fn boot(pkg: &[u8], mhz: u64) -> Result<Machine, String> {
    let st = std::panic::catch_unwind(|| fm1_soc::stock_from_package(pkg, None))
        .map_err(|_| "malformed firmware package".to_owned())??;
    if st.board != Board::Fm1 { return Err("this package is for the SMK-37, not the FM-1".into()); }
    let mut soc = Soc::new(RAM_SIZE, st.xip);
    soc.periph.hz = mhz * 1_000_000;
    soc.periph.skip_idle = true;
    soc.lcd_timed = false;
    let mut cpu = Cpu::new(st.entry);
    cpu.jit_on = true;
    cpu.user = true;
    let params = RAM_BASE + 0x7fc00;
    soc.write32(params, params + 0x40);
    cpu.regs[0] = fm1_soc::stock_boot(&mut soc, &st.flash, params, st.board);
    let flash0 = soc.flash.mem.clone();
    Ok(Machine { cpu, cpu1: None, soc, pending_tick: false, halted: false, flash0, error: String::new() })
}

fn write_err(out: *mut c_char, cap: c_int, msg: &str) {
    if out.is_null() || cap <= 0 { return; }
    let b = msg.as_bytes();
    let n = b.len().min(cap as usize - 1);
    unsafe {
        std::ptr::copy_nonoverlapping(b.as_ptr(), out as *mut u8, n);
        *out.add(n) = 0;
    }
}

/// Demarre un paquet .fwsc. `mhz` : horloge emulee (240 sur la FM-1). Renvoie null en cas d'erreur
/// (message dans `err`).
#[no_mangle]
pub extern "C" fn fm1hw_boot(pkg: *const u8, len: usize, mhz: u32, err: *mut c_char, err_cap: c_int) -> *mut Machine {
    if pkg.is_null() || len == 0 { write_err(err, err_cap, "empty package"); return std::ptr::null_mut(); }
    let data = unsafe { std::slice::from_raw_parts(pkg, len) };
    match std::panic::catch_unwind(|| boot(data, mhz.clamp(60, 480) as u64)) {
        Ok(Ok(m)) => Box::into_raw(Box::new(m)),
        Ok(Err(e)) => { write_err(err, err_cap, &e); std::ptr::null_mut() }
        Err(_) => { write_err(err, err_cap, "the emulator stopped while booting"); std::ptr::null_mut() }
    }
}

#[no_mangle]
pub extern "C" fn fm1hw_free(m: *mut Machine) {
    if !m.is_null() { unsafe { drop(Box::from_raw(m)); } }
}

/// Fait tourner le firmware (`blocks` blocs de code, ~20000 par appel comme fm1-emu) puis copie
/// l'audio produit (stereo entrelace, 16 bits) dans `out` (au plus `cap` trames). Renvoie le nombre
/// de trames, -1 si la machine s'est arretee (erreur du firmware ou de l'emulation).
#[no_mangle]
pub extern "C" fn fm1hw_run(m: *mut Machine, blocks: u32, out: *mut i16, cap: usize) -> c_int {
    let Some(m) = (unsafe { m.as_mut() }) else { return -1 };
    if m.halted { return -1; }
    let jit = m.cpu.jit_on;
    let r = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        let Machine { cpu, cpu1, soc, pending_tick, .. } = m;
        machine::run_dual(cpu1, soc, blocks.max(1), RAM_SIZE, jit, |soc, k| machine::run_blocks(cpu, soc, k, pending_tick))
    }));
    match r {
        Ok(Ok(_)) => {}
        Ok(Err(e)) => { m.halted = true; m.error = e; return -1; }
        Err(_) => { m.halted = true; m.error = "the emulator stopped".into(); return -1; }
    }
    let a = std::mem::take(&mut m.soc.periph.audio);
    let frames = (a.len() / 2).min(cap);
    if !out.is_null() && frames > 0 {
        unsafe { std::ptr::copy_nonoverlapping(a.as_ptr(), out, frames * 2); }
    }
    frames as c_int
}

/// Touche ou bouton de la matrice : 0 OCT-, 1 OCT+, 2..7 FX SEL ENV LFO EDIT GLO,
/// 8..13 HOME SAVE ARP SEQ PLAY REC, 14..40 notes FA3..SOL5.
#[no_mangle]
pub extern "C" fn fm1hw_key(m: *mut Machine, id: c_int, down: c_int, velocity: c_int) {
    if let Some(m) = unsafe { m.as_mut() } {
        if (14..41).contains(&id) && velocity > 0 { m.soc.periph.apply(&Action::KeyVel { vel: velocity.clamp(1, 127) as u8 }); }
        m.soc.periph.apply(&Action::Key { id: id.clamp(0, 40) as u8, down: down != 0 });
    }
}

/// Encodeur de la matrice : 0 SELECT, 1 ALGORITHM, 2..5 KNOB1..4, 6 PRESETS ; `clicks` > 0 = horaire.
#[no_mangle]
pub extern "C" fn fm1hw_encoder(m: *mut Machine, enc: c_int, clicks: c_int) {
    if let Some(m) = unsafe { m.as_mut() } { m.soc.periph.apply(&Action::Enc { enc: enc.clamp(0, 6) as u8, clicks }); }
}

/// Potentiometre MASTER (0..1023, voie 4 du convertisseur).
#[no_mangle]
pub extern "C" fn fm1hw_master(m: *mut Machine, value: c_int) {
    if let Some(m) = unsafe { m.as_mut() } { m.soc.periph.apply(&Action::Adc { ch: 4, value: value.clamp(0, 1023) as u16 }); }
}

/// Un octet MIDI dans la prise MIDI IN.
#[no_mangle]
pub extern "C" fn fm1hw_midi(m: *mut Machine, byte: u8) {
    if let Some(m) = unsafe { m.as_mut() } { m.soc.periph.apply(&Action::Midi { b: byte }); }
}

/// Ecran 240 x 240 en RGB565 (pointeur valable jusqu'au prochain fm1hw_run).
#[no_mangle]
pub extern "C" fn fm1hw_lcd(m: *mut Machine) -> *const u16 {
    unsafe { m.as_ref() }.map_or(std::ptr::null(), |m| m.soc.lcd.fb.as_ptr())
}

/// Allumage des 41 LED (meme numerotation que fm1hw_key), fraction de temps allume depuis l'appel precedent.
#[no_mangle]
pub extern "C" fn fm1hw_leds(m: *mut Machine, out: *mut f32) {
    if let (Some(m), false) = (unsafe { m.as_mut() }, out.is_null()) {
        let l = m.soc.periph.led_take();
        unsafe { std::ptr::copy_nonoverlapping(l.as_ptr(), out, 41); }
    }
}

/// Millisecondes emulees depuis le demarrage.
#[no_mangle]
pub extern "C" fn fm1hw_ms(m: *mut Machine) -> u64 {
    unsafe { m.as_ref() }.map_or(0, |m| m.soc.periph.ms())
}

/// La memoire flash ecrit-elle encore (sauvegarde en cours) ?
#[no_mangle]
pub extern "C" fn fm1hw_flash_busy(m: *mut Machine) -> c_int {
    unsafe { m.as_ref() }.map_or(0, |m| m.soc.flash.busy() as c_int)
}

/// Secteurs de 4096 octets que le firmware a modifies (ses sauvegardes) : appelle `f` pour chacun.
#[no_mangle]
pub extern "C" fn fm1hw_changed_sectors(m: *mut Machine, f: extern "C" fn(*mut std::ffi::c_void, u32, *const u8), user: *mut std::ffi::c_void) -> c_int {
    let Some(m) = (unsafe { m.as_ref() }) else { return 0 };
    let (now, then) = (&m.soc.flash.mem, &m.flash0);
    let mut n = 0;
    for s in 0..now.len().min(then.len()) / SECTOR {
        if now[s * SECTOR..(s + 1) * SECTOR] != then[s * SECTOR..(s + 1) * SECTOR] {
            f(user, (s * SECTOR) as u32, now[s * SECTOR..].as_ptr());
            n += 1;
        }
    }
    n
}

/// Reecrit un secteur sauvegarde (a l'adresse `at`, 4096 octets) avant de lancer le firmware.
#[no_mangle]
pub extern "C" fn fm1hw_restore_sector(m: *mut Machine, at: u32, data: *const u8) {
    let Some(m) = (unsafe { m.as_mut() }) else { return };
    if data.is_null() || at as usize % SECTOR != 0 || at as usize + SECTOR > m.soc.flash.mem.len() { return; }
    let d = unsafe { std::slice::from_raw_parts(data, SECTOR) };
    let at = at as usize;
    m.soc.flash.mem[at..at + SECTOR].copy_from_slice(d);
    let from = at.max(fm1_soc::XIP_FLASH_OFFSET);
    let end = (at + SECTOR).min(fm1_soc::XIP_FLASH_OFFSET + m.soc.xip.len());
    if from < end {
        m.soc.xip[from - fm1_soc::XIP_FLASH_OFFSET..end - fm1_soc::XIP_FLASH_OFFSET].copy_from_slice(&d[from - at..end - at]);
    }
}

/// Derniere erreur (machine arretee).
#[no_mangle]
pub extern "C" fn fm1hw_error(m: *mut Machine, out: *mut c_char, cap: c_int) {
    if let Some(m) = unsafe { m.as_ref() } { write_err(out, cap, &m.error); }
}

/// Le paquet est-il un firmware FM-1 (.fwsc) lisible ? 1 oui, 0 non (sans le demarrer).
#[no_mangle]
pub extern "C" fn fm1hw_probe(pkg: *const u8, len: usize) -> c_int {
    if pkg.is_null() || len == 0 { return 0; }
    let data = unsafe { std::slice::from_raw_parts(pkg, len) };
    match std::panic::catch_unwind(|| fm1_soc::stock_from_package(data, None)) {
        Ok(Ok(st)) if st.board == Board::Fm1 => 1,
        _ => 0,
    }
}
