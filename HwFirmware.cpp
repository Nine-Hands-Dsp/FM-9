// FM-9 (NINE HANDS | DSP) - firmwares materiels de la FM-1 (paquets .fwsc) dans l'emulateur de la puce.
//
// L'emulateur (fm1-emu de Joakim Eriksson, licence MIT, crates Rust dans third_party/fm1-emu, pont C dans fm1hw/)
// fait tourner les deux coeurs pi32v2 du JieLi AC791N, l'ecran, la flash, les touches et le son, exactement
// comme dans la FM-1. Comme dans le plugin de fm1-emu, la machine tourne dans son propre thread, un peu en
// avance sur l'hote : un anneau garde ses echantillons (44,1 kHz) et le MIDI y arrive date. Cette avance est
// annoncee a l'hote comme latence, que Reaper compense.
#include "FirmwareHost.h"
#include <thread>
#if JUCE_WINDOWS
 #include <windows.h>
 #pragma comment (lib, "winmm.lib")
#elif JUCE_MAC
 #include <pthread.h>
#endif

extern "C"
{
    struct Fm1Machine;
    Fm1Machine* fm1hw_boot (const uint8_t* pkg, size_t len, uint32_t mhz, char* err, int errCap);
    void fm1hw_free (Fm1Machine*);
    int fm1hw_run (Fm1Machine*, uint32_t blocks, int16_t* out, size_t cap);
    void fm1hw_key (Fm1Machine*, int id, int down, int velocity);
    void fm1hw_encoder (Fm1Machine*, int enc, int clicks);
    void fm1hw_master (Fm1Machine*, int value);
    void fm1hw_midi (Fm1Machine*, uint8_t b);
    const uint16_t* fm1hw_lcd (Fm1Machine*);
    void fm1hw_leds (Fm1Machine*, float* out41);
    uint64_t fm1hw_ms (Fm1Machine*);
    int fm1hw_flash_busy (Fm1Machine*);
    int fm1hw_changed_sectors (Fm1Machine*, void (*f) (void*, uint32_t, const uint8_t*), void* user);
    void fm1hw_restore_sector (Fm1Machine*, uint32_t at, const uint8_t* data);
    void fm1hw_error (Fm1Machine*, char* out, int cap);
    int fm1hw_probe (const uint8_t* pkg, size_t len);
}

namespace fm9
{
std::atomic<int>& FirmwareHost::hwMargin() { static std::atomic<int> m { 1536 }; return m; }   // automatique : part court, grandit si le DAW lit en avance
std::atomic<int>& FirmwareHost::hwMhz() { static std::atomic<int> m { 360 }; return m; }
std::atomic<bool>& FirmwareHost::hwLatencyChanged() { static std::atomic<bool> b { false }; return b; }
std::atomic<bool>& FirmwareHost::hwAutoMargin() { static std::atomic<bool> b { true }; return b; }

namespace
{
constexpr int sectorSize = 4096;

class HwFirmware final : public FirmwareHost
{
public:
    HwFirmware() { kind = Abi::hw; }

    bool start (const void* data, size_t size, const juce::File& storage, juce::String& error)
    {
        storageFile = storage;
        char err[256] = {};
        machine = fm1hw_boot ((const uint8_t*) data, size, (uint32_t) FirmwareHost::hwMhz().load(), err, (int) sizeof (err));
        if (machine == nullptr) { error = juce::String (err).isNotEmpty() ? juce::String (err) : juce::String ("the package does not start"); return false; }
        // sauvegardes du firmware (patterns, reglages) : secteurs de flash ecrits lors des sessions precedentes
        juce::MemoryBlock saved;
        if (storageFile.existsAsFile() && storageFile.loadFileAsData (saved))
        {
            const auto* p = (const uint8_t*) saved.getData();
            for (size_t o = 0; o + 4 + sectorSize <= saved.getSize(); o += 4 + sectorSize)
            {
                const uint32_t at = (uint32_t) p[o] | ((uint32_t) p[o + 1] << 8) | ((uint32_t) p[o + 2] << 16) | ((uint32_t) p[o + 3] << 24);
                fm1hw_restore_sector (machine, at, p + o + 4);
            }
            lastSaved = saved;
        }
        fm1hw_master (machine, 717);
        worker = std::thread ([this] { loop(); });
        return true;
    }

    ~HwFirmware() override
    {
        quit.store (true);
        if (worker.joinable()) worker.join();
        if (machine != nullptr)
        {
            collectSectors();
            saveIfDirty();
            fm1hw_free (machine);
        }
    }

    // ------------------------------------------------------------------ thread audio
    void prepare (double rate, int block) override
    {
        hostRate.store (rate > 0 ? rate : 48000.0);
        hostBlock.store (juce::jmax (32, block));
    }

    int latencySamples (double rate) const override
    {
        return juce::roundToInt ((double) leadFrames() * rate / fwRate);
    }

    void render (float* left, float* right, int n, double rate) override
    {
        if (rate <= 0) rate = 48000.0;
        hostRate.store (rate);
        {
            // avance du DAW sur le temps reel (rafales du calcul anticipe) : la marge suit, la latence est re-annoncee
            const double nowMs = juce::Time::getMillisecondCounterHiRes();
            if (lastRenderMs > 0.0)
            {
                ahead = juce::jmax (0.0, ahead + n - (nowMs - lastRenderMs) * rate / 1000.0);
                if (nowMs - lastRenderMs > 1500.0) ahead = 0.0;
                const int need = (int) std::ceil ((ahead * fwRate / rate + 1024.0) / 512.0) * 512;
                auto& mg = FirmwareHost::hwMargin();
                if (FirmwareHost::hwAutoMargin().load() && need > mg.load() && need <= 16384) { mg.store (need); FirmwareHost::hwLatencyChanged().store (true); }
            }
            lastRenderMs = nowMs;
        }
        lastTakeMs.store (juce::Time::getMillisecondCounter());
        const double step = fwRate / rate;
        const int64_t avail = writePos.load (std::memory_order_acquire);
        for (int i = 0; i < n; ++i)
        {
            const int64_t i0 = (int64_t) readPos;
            if (i0 + 3 > avail)
            {
                // la machine est en retard : on retombe doucement a zero au lieu d'un trou sec (pas de clic)
                holdL *= 0.995f; holdR *= 0.995f;
                left[i] = holdL; right[i] = holdR; ++starved; continue;
            }
            const float t = (float) (readPos - (double) i0);
            auto h = [t] (const std::vector<float>& b, int64_t k)
            {
                const float y0 = b[(size_t) ((k - 1) & (ring - 1))], y1 = b[(size_t) (k & (ring - 1))],
                            y2 = b[(size_t) ((k + 1) & (ring - 1))], y3 = b[(size_t) ((k + 2) & (ring - 1))];
                const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
                return ((c3 * t + c2) * t + c1) * t + y1;
            };
            left[i] = holdL = h (rl, i0 + 1);
            right[i] = holdR = h (rr, i0 + 1);
            readPos += step;
        }
        readFrame.store ((int64_t) readPos, std::memory_order_release);
    }

    void handleMidi (const juce::MidiMessage& m) override
    {
        if (m.isSysEx() || m.isMidiClock() || m.isActiveSense()) return;
        // date : ce que l'hote joue maintenant sortira 'lead' echantillons plus tard
        const int64_t due = readFrame.load() + leadFrames();
        const auto* d = m.getRawData();
        const int n = juce::jmin (3, m.getRawDataSize());
        for (int i = 0; i < n; ++i) push ({ evMidi, d[i], 0, due });
    }

    // ------------------------------------------------------------------ interface
    void button (int index, bool down) override
    {
        // FM-9 : 0-11 touches de fonction (FX..REC), 12 OCT-, 13 OCT+ ; matrice de la FM-1 : 0 OCT-, 1 OCT+, 2-13 FX..REC
        const int id = index < 12 ? index + 2 : index - 12;
        push ({ evKey, id, down ? 1 : 0, 0 });
    }
    void key (int index, bool down) override { push ({ evKey, 14 + juce::jlimit (0, 26, index), down ? 1 : 0, 0 }); }
    void encoder (int role, int steps) override
    {
        static const int encOf[7] = { 0, 1, 6, 2, 3, 4, 5 };      // SELECT, ALGORITHM, PRESETS, KNOB1-4 -> matrice
        push ({ evEnc, encOf[juce::jlimit (0, 6, role)], steps, 0 });
    }
    void master (float v) override { masterNorm.store (juce::jlimit (0.0f, 1.0f, v)); }
    void releaseAll() override { push ({ evRelease, 0, 0, 0 }); }

    void copyScreen (juce::Image& dest) override
    {
        if (! dest.isValid() || dest.getWidth() != fbW) dest = juce::Image (juce::Image::RGB, fbW, fbH, false);
        const juce::SpinLock::ScopedLockType sl (screenLock);
        juce::Image::BitmapData bd (dest, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < fbH; ++y)
            for (int x = 0; x < fbW; ++x)
            {
                const uint16_t p = screen[(size_t) (y * fbW + x)];
                const uint8_t r = (uint8_t) (((p >> 11) & 31) * 255 / 31), g = (uint8_t) (((p >> 5) & 63) * 255 / 63), b = (uint8_t) ((p & 31) * 255 / 31);
                bd.setPixelColour (x, y, juce::Colour (r, g, b));
            }
    }

    juce::String status() const override
    {
        const juce::SpinLock::ScopedLockType sl (errLock);
        if (error.isNotEmpty()) return error;
        return juce::SystemStats::getEnvironmentVariable ("FM9_HWDEBUG", {}).isNotEmpty()
                 ? "made " + juce::String (writePos.load()) + " read " + juce::String (readFrame.load()) + " starved " + juce::String (starved)
                 : juce::String();
    }

    void saveIfDirty() override
    {
        juce::MemoryBlock data;
        {
            const juce::SpinLock::ScopedLockType sl (sectorLock);
            if (! sectorsDirty) return;
            sectorsDirty = false;
            data = sectors;
        }
        if (storageFile == juce::File() || data == lastSaved) return;
        storageFile.getParentDirectory().createDirectory();
        if (storageFile.replaceWithData (data.getData(), data.getSize())) lastSaved = data;
    }

private:
    enum { evKey, evEnc, evMidi, evRelease };
    struct Event { int type, a, b; int64_t due; };
    static constexpr int ring = 32768;

    int leadFrames() const
    {
        // le bloc de l'hote (en echantillons de la machine) + une marge pour les blocs lents a emuler
        const double r = hostRate.load();
        return (int) std::ceil (hostBlock.load() * fwRate / (r > 0 ? r : 48000.0)) + FirmwareHost::hwMargin().load();
    }

    void push (const Event& e)
    {
        const juce::SpinLock::ScopedLockType sl (pushLock);
        int s1, n1, s2, n2;
        fifo.prepareToWrite (1, s1, n1, s2, n2);
        if (n1 > 0) events[(size_t) s1] = e; else if (n2 > 0) events[(size_t) s2] = e; else return;
        fifo.finishedWrite (1);
    }

    void takeEvents (std::vector<Event>& midi)
    {
        int s1, n1, s2, n2;
        fifo.prepareToRead (fifo.getNumReady(), s1, n1, s2, n2);
        auto apply = [&] (const Event& e)
        {
            if (e.type == evKey) { fm1hw_key (machine, e.a, e.b, 100); keysDown[(size_t) e.a] = e.b != 0; }
            else if (e.type == evEnc) fm1hw_encoder (machine, e.a, e.b);
            else if (e.type == evMidi) midi.push_back (e);
            else if (e.type == evRelease) { for (int k = 0; k < 41; ++k) if (keysDown[(size_t) k]) { fm1hw_key (machine, k, 0, 0); keysDown[(size_t) k] = false; } }
        };
        for (int i = 0; i < n1; ++i) apply (events[(size_t) (s1 + i)]);
        for (int i = 0; i < n2; ++i) apply (events[(size_t) (s2 + i)]);
        fifo.finishedRead (n1 + n2);
    }

    static void sectorCb (void* user, uint32_t at, const uint8_t* data)
    {
        auto& out = *static_cast<juce::MemoryOutputStream*> (user);
        out.writeInt ((int) at);
        out.write (data, sectorSize);
    }

    void collectSectors()
    {
        juce::MemoryOutputStream out;
        fm1hw_changed_sectors (machine, &HwFirmware::sectorCb, &out);
        const juce::SpinLock::ScopedLockType sl (sectorLock);
        juce::MemoryBlock mb (out.getData(), out.getDataSize());
        if (mb != sectors) { sectors = mb; sectorsDirty = true; }
    }

    static void raisePriority()
    {
        // le calcul de la puce passe avant les taches ordinaires (sinon le DAW le retarde et le son craque)
       #if JUCE_WINDOWS
        timeBeginPeriod (1);                       // pauses de 1 ms au lieu de 15,6 ms
        SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
       #elif JUCE_MAC
        pthread_set_qos_class_self_np (QOS_CLASS_USER_INTERACTIVE, 0);
       #endif
    }

    void loop()
    {
        raisePriority();
        std::vector<int16_t> buf (16384);
        std::vector<Event> due;
        std::vector<float> leds (41);
        int64_t made = 0;                          // echantillons poses dans l'anneau
        int lastPot = -1;
        double lastView = 0, lastFlash = juce::Time::getMillisecondCounterHiRes();
        uint64_t freeMs0 = 0; double freeT0 = 0; bool freeRun = false;
        while (! quit.load())
        {
            takeEvents (due);
            const int pot = juce::roundToInt (masterNorm.load() * 1023.0f);
            if (pot != lastPot) { fm1hw_master (machine, pot); lastPot = pot; }

            const int64_t fill = made - readFrame.load (std::memory_order_acquire);
            const int lead = leadFrames();
            const double now = juce::Time::getMillisecondCounterHiRes();
            const bool hostIdle = now - (double) lastTakeMs.load() > 1500.0;   // vraie pause (transport arrete) : pas les rafales du calcul anticipe de Reaper
            bool ran = false;
            if (fill < lead || (hostIdle && made == 0))
            {
                freeRun = false;
                // MIDI arrive a sa date (en echantillons produits)
                while (! due.empty() && due.front().due <= made) { fm1hw_midi (machine, (uint8_t) due.front().a); due.erase (due.begin()); }
                const int n = fm1hw_run (machine, 20000, buf.data(), buf.size() / 2);
                if (n < 0) { halt(); break; }
                for (int i = 0; i < n; ++i)
                {
                    const size_t k = (size_t) ((made + i) & (ring - 1));
                    rl[k] = buf[(size_t) (2 * i)] * (1.0f / 32768.0f);
                    rr[k] = buf[(size_t) (2 * i + 1)] * (1.0f / 32768.0f);
                }
                made += n;
                writePos.store (made, std::memory_order_release);
                ran = true;
            }
            else if (hostIdle)
            {
                // l'hote ne prend plus d'audio (piste arretee, fenetre seule) : la machine continue en temps reel,
                // son ecran vit, son audio est jete ; le MIDI en attente passe tout de suite
                if (! freeRun) { freeRun = true; freeT0 = now; freeMs0 = fm1hw_ms (machine); }
                for (auto& e : due) fm1hw_midi (machine, (uint8_t) e.a);
                due.clear();
                if ((double) (fm1hw_ms (machine) - freeMs0) < now - freeT0)
                {
                    if (fm1hw_run (machine, 20000, buf.data(), 0) < 0) { halt(); break; }
                    ran = true;
                }
            }
            if (now - lastView > 30.0)
            {
                lastView = now;
                publishView (leds);
            }
            if (now - lastFlash > 2000.0 && ! fm1hw_flash_busy (machine))
            {
                lastFlash = now;
                collectSectors();
            }
            if (! ran) std::this_thread::sleep_for (std::chrono::microseconds (500));
        }
       #if JUCE_WINDOWS
        timeEndPeriod (1);
       #endif
    }

    void halt()
    {
        char e[256] = {};
        fm1hw_error (machine, e, (int) sizeof (e));
        const juce::SpinLock::ScopedLockType sl (errLock);
        error = juce::String ("the firmware stopped: ") + juce::String (juce::CharPointer_UTF8 (e));
    }

    void publishView (std::vector<float>& leds)
    {
        if (const auto* lcd = fm1hw_lcd (machine))
        {
            const juce::SpinLock::ScopedLockType sl (screenLock);
            if (std::memcmp (screen.data(), lcd, sizeof (uint16_t) * (size_t) (fbW * fbH)) != 0)
            {
                std::memcpy (screen.data(), lcd, sizeof (uint16_t) * (size_t) (fbW * fbH));
                fbSerial.fetch_add (1);
            }
        }
        fm1hw_leds (machine, leds.data());
        // une LED pleinement allumee l'est ~1 tick sur 11 (balayage de la matrice)
        uint32_t b = 0, k = 0, d = 0;
        for (int id = 0; id < 41; ++id)
        {
            const float v = juce::jlimit (0.0f, 1.0f, leds[(size_t) id] / 0.08f);
            const bool on = v > 0.55f, dim = ! on && v > 0.08f;
            if (id < 2) { if (on) b |= 1u << (12 + id); }
            else if (id < 14) { if (on) b |= 1u << (id - 2); }
            else { if (on) k |= 1u << (id - 14); else if (dim) d |= 1u << (id - 14); }
        }
        litButtons.store (b); litKeys.store (k); dimKeys.store (d);
    }

    Fm1Machine* machine = nullptr;
    std::thread worker;
    std::atomic<bool> quit { false };
    juce::File storageFile;

    juce::AbstractFifo fifo { 1024 };
    std::array<Event, 1024> events {};
    juce::SpinLock pushLock;
    std::array<bool, 41> keysDown {};
    std::atomic<float> masterNorm { 0.7f };

    std::vector<float> rl = std::vector<float> (ring), rr = std::vector<float> (ring);
    std::atomic<int64_t> writePos { 0 }, readFrame { 0 };
    double readPos = 0.0;
    std::atomic<double> hostRate { 48000.0 };
    std::atomic<int> hostBlock { 512 };
    std::atomic<uint32_t> lastTakeMs { 0 };
    int64_t starved = 0;
    float holdL = 0.0f, holdR = 0.0f;
    double lastRenderMs = 0.0, ahead = 0.0;

    std::array<uint16_t, fbW * fbH> screen {};
    juce::SpinLock screenLock;

    juce::MemoryBlock sectors, lastSaved;
    bool sectorsDirty = false;
    juce::SpinLock sectorLock;

    juce::String error;
    mutable juce::SpinLock errLock;
};
} // namespace

std::unique_ptr<FirmwareHost> FirmwareHost::createHardware (const void* fwsc, size_t size, const juce::File& storage, juce::String& error)
{
    auto h = std::make_unique<HwFirmware>();
    if (! h->start (fwsc, size, storage, error)) return nullptr;
    return h;
}

static juce::Image rgb565ToImage (const uint16_t* fb, bool swapped)
{
    juce::Image img (juce::Image::RGB, FirmwareHost::fbW, FirmwareHost::fbH, false);
    juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < FirmwareHost::fbH; ++y)
        for (int x = 0; x < FirmwareHost::fbW; ++x)
        {
            uint16_t p = fb[(size_t) (y * FirmwareHost::fbW + x)];
            if (swapped) p = (uint16_t) ((p >> 8) | (p << 8));
            bd.setPixelColour (x, y, juce::Colour ((uint8_t) (((p >> 11) & 31) * 255 / 31), (uint8_t) (((p >> 5) & 63) * 255 / 63), (uint8_t) ((p & 31) * 255 / 31)));
        }
    return img;
}

juce::Image FirmwareHost::renderPreview (const void* data, size_t size, double seconds, std::function<void (float)> progress)
{
    if (isHardwarePackage (data, size))
    {
        char err[128] = {};
        auto* m = fm1hw_boot ((const uint8_t*) data, size, (uint32_t) FirmwareHost::hwMhz().load(), err, (int) sizeof (err));
        if (m == nullptr) return {};
        fm1hw_master (m, 0);
        const auto until = (uint64_t) (seconds * 1000.0);
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        while (fm1hw_ms (m) < until && juce::Time::getMillisecondCounterHiRes() - t0 < 15000.0)
        {
            if (fm1hw_run (m, 20000, nullptr, 0) < 0) break;
            if (progress) progress (juce::jlimit (0.0f, 1.0f, (float) fm1hw_ms (m) / (float) until));
        }
        juce::Image img;
        if (const auto* lcd = fm1hw_lcd (m)) img = rgb565ToImage (lcd, false);
        fm1hw_free (m);
        return img;
    }
    // version web : quelques secondes de son rendues (et jetees) pour que l'ecran s'installe
    juce::String err;
    auto fw = create (data, size, juce::File(), err);
    if (fw == nullptr) return {};
    fw->master (0.0f);
    std::vector<float> l (512), r (512);
    const int blocks = (int) (seconds * fwRate / 512.0);
    for (int i = 0; i < blocks; ++i)
    {
        fw->render (l.data(), r.data(), 512, fwRate);
        if (progress && (i & 15) == 0) progress ((float) i / (float) blocks);
    }
    juce::Image img;
    fw->copyScreen (img);
    return img;
}

bool FirmwareHost::isHardwarePackage (const void* data, size_t size)
{
    return data != nullptr && size > 1024 && fm1hw_probe ((const uint8_t*) data, size) == 1;
}
} // namespace fm9
