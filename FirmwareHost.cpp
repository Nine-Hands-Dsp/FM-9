// FM-9 (NINE HANDS | DSP) - hote de firmwares de la FM-1 (voir FirmwareHost.h)
#include "FirmwareHost.h"
extern "C" {
#include "wasm3/wasm3.h"
}
#include <cstdarg>
#include <chrono>

namespace fm9
{
// ------------------------------------------------------------------------------------------- imports
// seul import connu des firmwares web : l'horloge WASI
static m3ApiRawFunction (wasiClockTimeGet)
{
    m3ApiReturnType (uint32_t);
    m3ApiGetArg (uint32_t, clockId);
    m3ApiGetArg (uint64_t, precision);
    m3ApiGetArgMem (uint64_t*, out);
    juce::ignoreUnused (clockId, precision);
    m3ApiCheckMem (out, sizeof (uint64_t));
    const auto ns = (uint64_t) std::chrono::duration_cast<std::chrono::nanoseconds> (std::chrono::steady_clock::now().time_since_epoch()).count();
    std::memcpy (out, &ns, sizeof ns);
    m3ApiReturn (0);
}
static m3ApiRawFunction (stubReturn0) { m3ApiReturnType (uint32_t); m3ApiReturn (0); }
static m3ApiRawFunction (stubVoid) { m3ApiSuccess(); }

// ------------------------------------------------------------------------------------------- creation
std::unique_ptr<FirmwareHost> FirmwareHost::create (const void* wasm, size_t size, const juce::File& storage, juce::String& error)
{
    return WasmFirmware::make (wasm, size, storage, error);
}

std::unique_ptr<WasmFirmware> WasmFirmware::make (const void* wasm, size_t size, const juce::File& storage, juce::String& error)
{
    std::unique_ptr<WasmFirmware> h (new WasmFirmware());
    h->storageFile = storage;
    if (! h->start (wasm, size, error)) return nullptr;
    return h;
}

WasmFirmware::~WasmFirmware()
{
    if (rt != nullptr) m3_FreeRuntime (rt);
    if (env != nullptr) m3_FreeEnvironment (env);
}

M3Function* WasmFirmware::fn (const char* name)
{
    IM3Function f = nullptr;
    if (m3_FindFunction (&f, rt, name) != m3Err_none) return nullptr;
    return f;
}

bool WasmFirmware::call (M3Function* f, ...)
{
    if (f == nullptr) return false;
    va_list a;
    va_start (a, f);
    const M3Result r = m3_CallVL (f, a);
    va_end (a);
    return r == m3Err_none;
}

uint32_t WasmFirmware::callU (M3Function* f)
{
    if (f == nullptr || m3_CallV (f) != m3Err_none) return 0;
    uint32_t v = 0;
    m3_GetResultsV (f, &v);
    return v;
}

uint8_t* WasmFirmware::mem (size_t* size)
{
    size_t s = 0;
    auto* m = m3_GetMemory (mod, &s, 0);
    if (size != nullptr) *size = s;
    return m;
}

bool WasmFirmware::start (const void* wasm, size_t size, juce::String& error)
{
    wasmBytes.assign ((const uint8_t*) wasm, (const uint8_t*) wasm + size);
    env = m3_NewEnvironment();
    rt = m3_NewRuntime (env, 8 * 1024 * 1024, nullptr);
    if (env == nullptr || rt == nullptr) { error = "wasm runtime"; return false; }
    if (auto r = m3_ParseModule (env, &mod, wasmBytes.data(), (uint32_t) wasmBytes.size())) { error = r; return false; }
    if (auto r = m3_LoadModule (rt, mod)) { error = r; m3_FreeModule (mod); mod = nullptr; return false; }
    m3_LinkRawFunction (mod, "wasi_snapshot_preview1", "clock_time_get", "i(iIi)", &wasiClockTimeGet);
    m3_LinkRawFunction (mod, "wasi_snapshot_preview1", "proc_exit", "v(i)", &stubVoid);
    m3_LinkRawFunction (mod, "wasi_snapshot_preview1", "fd_write", "i(iiii)", &stubReturn0);
    m3_LinkRawFunction (mod, "wasi_snapshot_preview1", "fd_close", "i(i)", &stubReturn0);
    m3_LinkRawFunction (mod, "wasi_snapshot_preview1", "fd_seek", "i(iIii)", &stubReturn0);
    m3_LinkRawFunction (mod, "env", "emscripten_notify_memory_growth", "v(i)", &stubVoid);

    if (auto* init = fn ("_initialize")) call (init);

    if (fn ("sim_render") != nullptr)
    {
        kind = Abi::sim;
        fRender = fn ("sim_render"); fAudio = fn ("sim_audio"); fBtn = fn ("sim_btn"); fKey = fn ("sim_key");
        fEnc = fn ("sim_enc"); fMaster = fn ("sim_master"); fFb = fn ("sim_fb"); fLeds = fn ("sim_leds"); fKeyLeds = fn ("sim_key_leds");
        fFlash = fn ("sim_flash"); fFlashDirty = fn ("sim_flash_dirty"); fFlashClean = fn ("sim_flash_clean");
        masterMax = 1023.0f;
        auto* fInit = fn ("sim_init");
        auto* fReset = fn ("sim_flash_reset");
        if (fInit == nullptr || fAudio == nullptr) { error = "sim_init"; return false; }
        // memoire flash sauvegardee (1 Mo) : on la remet avant le demarrage
        juce::MemoryBlock saved;
        const size_t flashSize = 0x100000;
        bool restored = false;
        if (fFlash != nullptr && storageFile.existsAsFile() && storageFile.loadFileAsData (saved) && saved.getSize() == flashSize)
        {
            call (fReset);
            const uint32_t p = callU (fFlash);
            size_t ms = 0; auto* m = mem (&ms);
            if (m != nullptr && p + flashSize <= ms) { std::memcpy (m + p, saved.getData(), flashSize); restored = true; }
        }
        if (! call (fInit, restored ? 0 : 1)) { error = "sim_init"; return false; }
        storageMirror.assign (flashSize, 0xff);
        if (restored) std::memcpy (storageMirror.data(), saved.getData(), flashSize);
    }
    else if (fn ("web_render") != nullptr)
    {
        kind = Abi::web;
        fRender = fn ("web_render"); fOutL = fn ("web_out_l"); fOutR = fn ("web_out_r");
        fButtons = fn ("web_buttons"); fKeys = fn ("web_keys"); fEnc = fn ("web_enc"); fMaster = fn ("web_master");
        fLeds = fn ("web_lit_buttons"); fKeyLeds = fn ("web_lit_keys"); fDimKeys = fn ("web_dim_keys"); fMidi = fn ("web_midi");
        fFb = fn ("web_fb");
        if (fFb != nullptr) { fBlits = fn ("web_blits"); masterMax = 4096.0f; fbSwapped = true; }
        else { fFb = fn ("web_screen"); fBlits = fn ("web_screen_draws"); masterMax = 1023.0f; fbSwapped = true; }
        fNor = fn ("web_nor"); fNorSize = fn ("web_nor_size"); fNorWrites = fn ("web_flash_writes_count");
        fStore = fn ("web_store"); fStoreLen = fn ("web_store_len"); fStoreSetLen = fn ("web_store_set_len");
        fStoreWrites = fn ("web_store_writes"); fNobj = fn ("web_nobj"); fStoreMax = fn ("web_store_max");
        if (fOutL == nullptr || fOutR == nullptr) { error = "web_out"; return false; }

        juce::MemoryBlock saved;
        const bool have = storageFile.existsAsFile() && storageFile.loadFileAsData (saved);
        size_t ms = 0; auto* m = mem (&ms);
        if (fNor != nullptr && fNorSize != nullptr)
        {
            const uint32_t p = callU (fNor), n = callU (fNorSize);
            storageMirror.assign (n, 0xff);
            if (have && saved.getSize() == n && m != nullptr && p + n <= ms) { std::memcpy (m + p, saved.getData(), n); std::memcpy (storageMirror.data(), saved.getData(), n); }
        }
        else if (fStore != nullptr && fNobj != nullptr && fStoreMax != nullptr)
        {
            // objets sauvegardes : [longueur int32][donnees x max] par objet
            const uint32_t nobj = callU (fNobj), maxLen = callU (fStoreMax);
            storageMirror.assign ((size_t) nobj * (4 + maxLen), 0);
            if (have && saved.getSize() == storageMirror.size())
            {
                std::memcpy (storageMirror.data(), saved.getData(), saved.getSize());
                for (uint32_t o = 0; o < nobj; ++o)
                {
                    const uint8_t* rec = storageMirror.data() + (size_t) o * (4 + maxLen);
                    int32_t len = 0; std::memcpy (&len, rec, 4);
                    if (fStore != nullptr && m3_CallV (fStore, (int) o) == m3Err_none)
                    {
                        uint32_t p = 0; m3_GetResultsV (fStore, &p);
                        m = mem (&ms);
                        if (len > 0 && p != 0 && p + (uint32_t) len <= ms) std::memcpy (m + p, rec + 4, (size_t) len);
                        call (fStoreSetLen, (int) o, (int) len);
                    }
                }
            }
        }
        if (! call (fn ("web_boot"))) { error = "web_boot"; return false; }
    }
    else { error = "not an FM-1 firmware (no sim_render / web_render)"; return false; }

    // un premier rendu pour l'ecran
    renderChunk (256);
    captureScreen();
    return true;
}

// ------------------------------------------------------------------------------------------- evenements
void WasmFirmware::push (int type, int a, int b)
{
    const juce::SpinLock::ScopedLockType sl (pushLock);
    int s1, n1, s2, n2;
    fifo.prepareToWrite (1, s1, n1, s2, n2);
    if (n1 > 0) events[(size_t) s1] = { type, a, b };
    else if (n2 > 0) events[(size_t) s2] = { type, a, b };
    fifo.finishedWrite (n1 + n2);
}

void WasmFirmware::applyEvents()
{
    int s1, n1, s2, n2;
    fifo.prepareToRead (fifo.getNumReady(), s1, n1, s2, n2);
    auto apply = [this] (const Event& e)
    {
        switch (e.type)
        {
            case evButton:
                if (kind == Abi::sim) call (fBtn, e.a, e.b);
                else { btnMask = e.b ? (btnMask | (1u << e.a)) : (btnMask & ~(1u << e.a)); call (fButtons, (int) btnMask); }
                break;
            case evKey:
                if (e.a < 0 || e.a > 26) break;
                if (kind == Abi::sim) call (fKey, e.a, e.b);
                else { keyMask = e.b ? (keyMask | (1u << e.a)) : (keyMask & ~(1u << e.a)); call (fKeys, (int) keyMask); }
                break;
            case evEnc: call (fEnc, e.a, e.b); break;
            case evMidi: call (fMidi, e.a & 0xff, (e.a >> 8) & 0xff, e.b); break;
            case evReleaseAll:
                if (kind == Abi::sim) { for (int b = 0; b < 14; ++b) call (fBtn, b, 0); for (int k = 0; k < 27; ++k) call (fKey, k, 0); }
                else { btnMask = keyMask = 0; call (fButtons, 0); call (fKeys, 0); }
                break;
            default: break;
        }
    };
    for (int i = 0; i < n1; ++i) apply (events[(size_t) (s1 + i)]);
    for (int i = 0; i < n2; ++i) apply (events[(size_t) (s2 + i)]);
    fifo.finishedRead (n1 + n2);
}

void WasmFirmware::handleMidi (const juce::MidiMessage& m)
{
    // famille Felucca : le firmware a son propre MIDI ; sinon les notes jouent les touches de la facade (F3-G5)
    if (fMidi != nullptr && m.getRawDataSize() <= 3 && ! m.isSysEx())
    {
        const auto* d = m.getRawData();
        const int n = m.getRawDataSize();
        call (fMidi, d[0], n > 1 ? d[1] : 0, n > 2 ? d[2] : 0);
        return;
    }
    if (m.isNoteOn() || m.isNoteOff())
    {
        const int k = m.getNoteNumber() - 53;
        if (k < 0 || k > 26) return;
        const bool down = m.isNoteOn();
        if (kind == Abi::sim) call (fKey, k, down ? 1 : 0);
        else { keyMask = down ? (keyMask | (1u << k)) : (keyMask & ~(1u << k)); call (fKeys, (int) keyMask); }
    }
}

// ------------------------------------------------------------------------------------------- audio
void WasmFirmware::renderChunk (int frames)
{
    uint32_t ui = 0;
    if (kind == Abi::sim)
    {
        if (m3_CallV (fRender, frames) != m3Err_none) { frames = 0; }
        else m3_GetResultsV (fRender, &ui);
        const uint32_t p = callU (fAudio);
        size_t ms = 0; auto* m = mem (&ms);
        const float* a = (m != nullptr && p + (size_t) frames * 8 <= ms) ? (const float*) (m + p) : nullptr;
        for (int i = 0; i < frames; ++i)
        {
            const int w = (int) ((written + i) % ring);
            rl[(size_t) w] = a != nullptr ? a[2 * i] : 0.0f;
            rr[(size_t) w] = a != nullptr ? a[2 * i + 1] : 0.0f;
        }
        if (ui > 0) lastBlits = 0xffffffffu;           // nouvelle image a recopier
    }
    else
    {
        if (! call (fRender, frames)) frames = 0;
        const uint32_t pl = callU (fOutL), pr = callU (fOutR);
        size_t ms = 0; auto* m = mem (&ms);
        const bool ok = m != nullptr && pl + (size_t) frames * 4 <= ms && pr + (size_t) frames * 4 <= ms;
        for (int i = 0; i < frames; ++i)
        {
            const int w = (int) ((written + i) % ring);
            rl[(size_t) w] = ok ? ((const float*) (m + pl))[i] : 0.0f;
            rr[(size_t) w] = ok ? ((const float*) (m + pr))[i] : 0.0f;
        }
    }
    written += frames;
}

void WasmFirmware::render (float* left, float* right, int n, double hostRate)
{
    applyEvents();
    const int mv = juce::roundToInt (masterNorm.load() * masterMax);
    if (mv != lastMaster) { lastMaster = mv; call (fMaster, mv); }

    const double step = fwRate / juce::jmax (8000.0, hostRate);
    const double endPos = readPos + step * n;
    int guard = 0;
    while ((double) written < endPos + 3.0 && guard++ < 64) renderChunk (256);
    if ((double) written < endPos + 3.0) { juce::FloatVectorOperations::clear (left, n); juce::FloatVectorOperations::clear (right, n); return; }
    // le lecteur ne doit pas trainer plus loin que l'anneau
    if ((double) written - readPos > ring - 1024) readPos = (double) written - 2048.0;

    auto at = [this] (const std::vector<float>& b, int64_t i) { return b[(size_t) (((i % ring) + ring) % ring)]; };
    for (int j = 0; j < n; ++j)
    {
        const double p = readPos + step * j;
        const int64_t i = (int64_t) std::floor (p);
        const float t = (float) (p - (double) i);
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto& b = ch == 0 ? rl : rr;
            const float y0 = at (b, i - 1), y1 = at (b, i), y2 = at (b, i + 1), y3 = at (b, i + 2);
            const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
            const float v = ((c3 * t + c2) * t + c1) * t + y1;
            (ch == 0 ? left : right)[j] = std::isfinite (v) ? juce::jlimit (-4.0f, 4.0f, v) : 0.0f;
        }
    }
    readPos = endPos;

    // LED, ecran (30 images/s au plus), sauvegarde (toutes les secondes)
    litButtons.store (callU (fLeds));
    litKeys.store (callU (fKeyLeds));
    if (fDimKeys != nullptr) dimKeys.store (callU (fDimKeys));
    const double dt = n / juce::jmax (8000.0, hostRate);
    sinceScreen += dt; sinceStorage += dt;
    if (sinceScreen >= 1.0 / 30.0) { sinceScreen = 0; captureScreen(); }
    if (sinceStorage >= 1.0) { sinceStorage = 0; captureStorage (false); }
}

void WasmFirmware::captureScreen()
{
    if (kind == Abi::web && fBlits != nullptr)
    {
        const uint32_t b = callU (fBlits);
        if (b == lastBlits) return;
        lastBlits = b;
    }
    else if (kind == Abi::sim)
    {
        if (lastBlits != 0xffffffffu && fbSerial.load() > 0) return;
        lastBlits = 0;
    }
    const uint32_t p = callU (fFb);
    size_t ms = 0; auto* m = mem (&ms);
    if (m == nullptr || p == 0 || p + (size_t) fbW * fbH * 2 > ms) return;
    const int back = 1 - fbFront.load();
    std::memcpy (fb[(size_t) back].data(), m + p, (size_t) fbW * fbH * 2);
    fbFront.store (back);
    fbSerial.fetch_add (1);
}

void WasmFirmware::copyScreen (juce::Image& dest)
{
    if (! dest.isValid() || dest.getWidth() != fbW) dest = juce::Image (juce::Image::RGB, fbW, fbH, false);
    const auto& src = fb[(size_t) fbFront.load()];
    juce::Image::BitmapData bd (dest, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < fbH; ++y)
        for (int x = 0; x < fbW; ++x)
        {
            uint16_t c = src[(size_t) (y * fbW + x)];
            if (fbSwapped) c = (uint16_t) ((c >> 8) | (c << 8));
            const uint8_t r = (uint8_t) ((((c >> 11) & 31) * 527 + 23) >> 6), g = (uint8_t) ((((c >> 5) & 63) * 259 + 33) >> 6), b = (uint8_t) (((c & 31) * 527 + 23) >> 6);
            bd.setPixelColour (x, y, juce::Colour (r, g, b));
        }
}

// ------------------------------------------------------------------------------------------- sauvegarde
void WasmFirmware::captureStorage (bool force)
{
    if (storageMirror.empty()) return;
    const juce::SpinLock::ScopedTryLockType tl (storageLock);
    if (! tl.isLocked()) return;
    size_t ms = 0; auto* m = mem (&ms);
    if (m == nullptr) return;
    if (kind == Abi::sim && fFlashDirty != nullptr)
    {
        const uint32_t pd = callU (fFlashDirty), pf = callU (fFlash);
        m = mem (&ms);
        if (pd == 0 || pf + storageMirror.size() > ms) return;
        const uint32_t* bits = (const uint32_t*) (m + pd);
        bool any = false;
        for (int s = 0; s < 256; ++s)
            if ((bits[s >> 5] >> (s & 31)) & 1u) { std::memcpy (storageMirror.data() + s * 4096, m + pf + s * 4096, 4096); any = true; }
        if (any) { call (fFlashClean); storageDirty.store (true); }
    }
    else if (fNor != nullptr)
    {
        const uint32_t w = callU (fNorWrites);
        if (! force && w == lastWrites) return;
        lastWrites = w;
        const uint32_t p = callU (fNor);
        m = mem (&ms);
        if (p + storageMirror.size() <= ms) { std::memcpy (storageMirror.data(), m + p, storageMirror.size()); storageDirty.store (true); }
    }
    else if (fStore != nullptr && fNobj != nullptr)
    {
        const uint32_t w = callU (fStoreWrites);
        if (! force && w == lastWrites) return;
        lastWrites = w;
        const uint32_t nobj = callU (fNobj), maxLen = callU (fStoreMax);
        for (uint32_t o = 0; o < nobj; ++o)
        {
            uint8_t* rec = storageMirror.data() + (size_t) o * (4 + maxLen);
            int32_t len = -1; uint32_t p = 0;
            if (m3_CallV (fStoreLen, (int) o) == m3Err_none) m3_GetResultsV (fStoreLen, &len);
            if (m3_CallV (fStore, (int) o) == m3Err_none) m3_GetResultsV (fStore, &p);
            m = mem (&ms);
            std::memcpy (rec, &len, 4);
            if (len > 0 && (uint32_t) len <= maxLen && p + (uint32_t) len <= ms) std::memcpy (rec + 4, m + p, (size_t) len);
        }
        storageDirty.store (true);
    }
}

void WasmFirmware::saveIfDirty()
{
    if (! storageDirty.load()) return;
    std::vector<uint8_t> copy;
    {
        const juce::SpinLock::ScopedLockType sl (storageLock);
        copy = storageMirror;
        storageDirty.store (false);
    }
    storageFile.getParentDirectory().createDirectory();
    storageFile.replaceWithData (copy.data(), copy.size());
}

// ------------------------------------------------------------------------------------------- installation
juce::File FirmwareInstaller::folder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Nine Hands DSP").getChildFile ("FM-9").getChildFile ("Firmwares");
}

int FirmwareInstaller::remove (const juce::String& name)
{
    // version web, paquet materiel, notes, sauvegardes (.hwflash, .sav...) et apercu : tout ce qui porte ce nom
    const auto stem = name.endsWith (hardwareSuffix) ? name.dropLastCharacters (juce::String (hardwareSuffix).length()) : name;
    if (stem.isEmpty()) return 0;
    int n = 0;
    for (auto& f : folder().findChildFiles (juce::File::findFiles, false))
        if (f.getFileName().startsWith (stem + ".") && f.deleteFile()) ++n;
    if (folder().getChildFile ("Previews").getChildFile (stem + ".png").deleteFile()) ++n;
    return n;
}

juce::StringArray FirmwareInstaller::installed()
{
    juce::StringArray s;
    for (auto& f : folder().findChildFiles (juce::File::findFiles, false, "*.wasm")) s.add (f.getFileNameWithoutExtension());
    s.sort (true);
    juce::StringArray hw;
    for (auto& f : folder().findChildFiles (juce::File::findFiles, false, "*.fwsc")) hw.add (f.getFileNameWithoutExtension() + hardwareSuffix);
    hw.sort (true);
    s.addArray (hw);
    return s;
}

juce::File FirmwareInstaller::installPackage (const juce::File& src, juce::String& error)
{
    juce::MemoryBlock mb;
    if (! src.loadFileAsData (mb)) { error = "cannot read " + src.getFileName(); return {}; }
    juce::String ext;
    if (abiOf (mb.getData(), mb.getSize()) != 0) ext = ".wasm";
    else if (FirmwareHost::isHardwarePackage (mb.getData(), mb.getSize())) ext = ".fwsc";
    else { error = "this file is not an FM-1 firmware (web build .wasm or update package .fwsc)"; return {}; }
    folder().createDirectory();
    const auto dest = folder().getChildFile (juce::File::createLegalFileName (src.getFileNameWithoutExtension()) + ext);
    if (dest != src && ! dest.replaceWithData (mb.getData(), mb.getSize())) { error = "cannot write " + dest.getFullPathName(); return {}; }
    return dest;
}

bool FirmwareInstaller::parseRepo (const juce::String& text, juce::String& owner, juce::String& repo)
{
    auto t = text.trim();
    if (t.containsIgnoreCase ("github.io"))
    {
        // https://owner.github.io/repo/...
        auto host = t.fromFirstOccurrenceOf ("://", false, false);
        if (host.isEmpty()) host = t;
        owner = host.upToFirstOccurrenceOf (".github.io", false, true);
        repo = host.fromFirstOccurrenceOf (".github.io/", false, true).upToFirstOccurrenceOf ("/", false, false);
    }
    else
    {
        if (t.containsIgnoreCase ("github.com/")) t = t.fromFirstOccurrenceOf ("github.com/", false, true);
        auto parts = juce::StringArray::fromTokens (t, "/", "");
        parts.removeEmptyStrings();
        if (parts.size() >= 2) { owner = parts[0]; repo = parts[1]; }
    }
    repo = repo.upToFirstOccurrenceOf (".git", false, false).upToFirstOccurrenceOf ("#", false, false).upToFirstOccurrenceOf ("?", false, false);
    return owner.isNotEmpty() && repo.isNotEmpty() && ! owner.containsAnyOf (" \\") && ! repo.containsAnyOf (" \\");
}

int FirmwareInstaller::abiOf (const void* data, size_t size)
{
    // section des exports du module : cherche sim_render / web_render
    const auto* d = (const uint8_t*) data;
    if (size < 8 || std::memcmp (d, "\0asm", 4) != 0) return 0;
    size_t p = 8;
    auto leb = [&] (size_t& q) { uint32_t r = 0; int s = 0; while (q < size) { const uint8_t b = d[q++]; r |= (uint32_t) (b & 0x7f) << s; s += 7; if (b < 0x80) break; } return r; };
    int found = 0;
    while (p < size)
    {
        const uint8_t id = d[p++];
        const uint32_t len = leb (p);
        const size_t end = p + len;
        if (end > size) break;
        if (id == 7)
        {
            size_t q = p;
            const uint32_t n = leb (q);
            for (uint32_t i = 0; i < n && q < end; ++i)
            {
                const uint32_t nl = leb (q);
                if (q + nl > end) break;
                const juce::String name (juce::CharPointer_UTF8 ((const char*) d + q), nl);
                q += nl; q += 1; leb (q);
                if (name == "sim_render") found = 1;
                else if (name == "web_render" && found == 0) found = 2;
            }
            return found;
        }
        p = end;
    }
    return 0;
}

static bool fetch (const juce::String& url, juce::MemoryBlock& out, juce::String& error)
{
    juce::URL u (url);
    int status = 0;
    auto opts = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                    .withExtraHeaders ("User-Agent: FM-9 (NINE HANDS | DSP)\r\nAccept: application/vnd.github+json")
                    .withConnectionTimeoutMs (20000).withStatusCode (&status).withNumRedirectsToFollow (5);
    auto in = u.createInputStream (opts);
    if (in == nullptr) { error = "network error (" + url + ")"; return false; }
    out.reset();
    in->readIntoMemoryBlock (out);
    if (status >= 400) { error = "HTTP " + juce::String (status); return false; }
    return true;
}

juce::File FirmwareInstaller::install (const juce::String& owner, const juce::String& repo, juce::String& error,
                                       std::function<void (const juce::String&)> progress)
{
    auto say = [&] (const juce::String& s) { if (progress) progress (s); };
    // 1. liste des fichiers : branche gh-pages (emulateur web), sinon branche par defaut
    juce::StringArray branches { "gh-pages" };
    {
        juce::MemoryBlock info; juce::String e;
        if (fetch ("https://api.github.com/repos/" + owner + "/" + repo, info, e))
        {
            auto v = juce::JSON::parse (info.toString());
            const auto def = v.getProperty ("default_branch", "main").toString();
            if (def.isNotEmpty() && def != "gh-pages") branches.add (def);
        }
        else { error = "repository not found: " + owner + "/" + repo + " (" + e + ")"; return {}; }
    }
    juce::StringArray packages;     // .fwsc trouves (branche + chemin), essayes si aucune version web ne convient
    juce::StringArray packageBranches;
    for (auto& br : branches)
    {
        say ("Looking for the firmware in " + br + "...");
        const int before = packages.size();
        juce::MemoryBlock tree; juce::String e;
        if (! fetch ("https://api.github.com/repos/" + owner + "/" + repo + "/git/trees/" + br + "?recursive=1", tree, e)) continue;
        auto v = juce::JSON::parse (tree.toString());
        juce::StringArray candidates;
        if (auto* arr = v.getProperty ("tree", {}).getArray())
            for (auto& item : *arr)
            {
                const auto path = item.getProperty ("path", {}).toString();
                const auto low = path.toLowerCase();
                if (low.endsWith (".fwsc"))
                {
                    // paquet materiel (le firmware que l'on flashe dans la FM-1) : pas ceux d'origine de M-VAVE embarques pour la restauration
                    const auto fileLow = low.fromLastOccurrenceOf ("/", false, false);
                    if (! (fileLow.startsWith ("fm-1") || fileLow.contains ("original") || fileLow.contains ("stock") || low.contains ("restore")))
                        packages.add (path);
                    continue;
                }
                if (! low.endsWith (".wasm")) continue;
                if (low.contains ("espeak") || low.contains ("flac") || low.contains ("vendor") || low.contains ("reader") || low.contains ("node_modules")) continue;
                candidates.add (path);
            }
        while (packageBranches.size() < packages.size()) packageBranches.add (br);
        juce::ignoreUnused (before);
        // les emulateurs d'abord
        std::stable_sort (candidates.begin(), candidates.end(), [] (const juce::String& a, const juce::String& b)
        {
            auto score = [] (const juce::String& s) { const auto l = s.toLowerCase(); return (l.contains ("emu") ? 2 : 0) + (l.contains ("sim") ? 1 : 0); };
            return score (a) > score (b);
        });
        for (auto& path : candidates)
        {
            say ("Downloading " + path + "...");
            juce::MemoryBlock wasm;
            if (! fetch ("https://raw.githubusercontent.com/" + owner + "/" + repo + "/" + br + "/" + path, wasm, e)) continue;
            if (abiOf (wasm.getData(), wasm.getSize()) == 0) continue;
            // verifie qu'il demarre vraiment
            juce::String err;
            auto test = FirmwareHost::create (wasm.getData(), wasm.getSize(), juce::File(), err);
            if (test == nullptr) { e = err; continue; }
            folder().createDirectory();
            auto f = folder().getChildFile (juce::File::createLegalFileName (repo) + ".wasm");
            f.replaceWithData (wasm.getData(), wasm.getSize());
            f.withFileExtension ("txt").replaceWithText ("source: https://github.com/" + owner + "/" + repo + "\nfile: " + br + "/" + path
                                                         + "\nlicense: see the repository (GPL-3.0 for the Felucca family)\n");
            say ("Installed");
            return f;
        }
    }
    // pas de version web : le paquet materiel le plus recent (nom le plus grand), lance dans l'emulateur de la puce
    for (int i = packages.size(); --i >= 0;)
    {
        juce::String e;
        say ("Downloading " + packages[i] + "...");
        juce::MemoryBlock pkg;
        if (! fetch ("https://raw.githubusercontent.com/" + owner + "/" + repo + "/" + packageBranches[i] + "/" + packages[i], pkg, e)) continue;
        if (! FirmwareHost::isHardwarePackage (pkg.getData(), pkg.getSize())) continue;
        folder().createDirectory();
        auto f = folder().getChildFile (juce::File::createLegalFileName (repo) + ".fwsc");
        f.replaceWithData (pkg.getData(), pkg.getSize());
        f.withFileExtension ("txt").replaceWithText ("source: https://github.com/" + owner + "/" + repo + "\npackage: " + packageBranches[i] + "/" + packages[i]
                                                     + "\nlicense: see the repository\n");
        say ("Installed");
        return f;
    }
    error = "no FM-1 firmware (update package .fwsc or web build .wasm) found in " + owner + "/" + repo;
    return {};
}
} // namespace fm9
