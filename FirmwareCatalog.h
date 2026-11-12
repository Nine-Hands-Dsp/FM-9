// FM-9 (NINE HANDS | DSP) - catalogue des firmwares de la FM-1.
// Liste tiree du hub github.com/aroum/fm1-custom-fw par tools/gen_fw_catalog.py : chaque firmware est
// marque compatible (version web .wasm qui demarre dans FM-9) ou non (avec la raison).
// Copie integree au plugin, mise a jour depuis le depot de FM-9 (bouton Refresh).
#pragma once
#include <JuceHeader.h>
#include "FirmwareHost.h"
#include "NineHandsMenu.h"
#include <map>
#include <set>
#include <thread>

namespace fm9
{
struct CatalogEntry
{
    juce::String name, author, group, desc, repo, page, reason, url, abi, pkg, license;
    bool compatible = false, vendor = false;
    juce::int64 size = 0;
    // nom des fichiers installes : le nom du firmware (les forks partagent souvent le nom du depot)
    juce::String fileName() const
    {
        return juce::File::createLegalFileName (name.isNotEmpty() ? name : repo.fromFirstOccurrenceOf ("/", false, false)).trim();
    }
    bool hardware() const { return pkg.isNotEmpty(); }
    bool web() const { return url.isNotEmpty(); }
    bool isInstalled (const juce::StringArray& inst) const
    {
        return inst.contains (fileName()) || inst.contains (fileName() + FirmwareInstaller::hardwareSuffix);
    }
    // ce que Load lance : le paquet materiel (le vrai firmware) s'il est installe, sinon la version web
    juce::String loadName (const juce::StringArray& inst) const
    {
        return inst.contains (fileName() + FirmwareInstaller::hardwareSuffix) ? fileName() + FirmwareInstaller::hardwareSuffix : fileName();
    }
};

struct FirmwareCatalog
{
    static constexpr const char* remoteUrl = "https://raw.githubusercontent.com/Nine-Hands-Dsp/FM-9/main/catalog/firmwares.json";
    static juce::File cacheFile() { return FirmwareInstaller::folder().getChildFile ("catalog.json"); }

    static std::vector<CatalogEntry> parse (const juce::String& json, juce::String* date = nullptr)
    {
        std::vector<CatalogEntry> out;
        const auto v = juce::JSON::parse (json);
        if (date != nullptr) *date = v.getProperty ("generated", {}).toString();
        if (auto* arr = v.getProperty ("firmwares", {}).getArray())
            for (auto& f : *arr)
            {
                CatalogEntry e;
                e.name = f.getProperty ("name", {}).toString();     e.author = f.getProperty ("author", {}).toString();
                e.group = f.getProperty ("group", {}).toString();   e.desc = f.getProperty ("desc", {}).toString();
                e.repo = f.getProperty ("repo", {}).toString();     e.page = f.getProperty ("page", {}).toString();
                e.reason = f.getProperty ("reason", {}).toString(); e.url = f.getProperty ("url", {}).toString();
                e.abi = f.getProperty ("abi", {}).toString();       e.compatible = (bool) f.getProperty ("compatible", false);
                e.size = (juce::int64) f.getProperty ("size", 0);
                e.pkg = f.getProperty ("pkg", {}).toString();       e.license = f.getProperty ("license", {}).toString();
                e.vendor = (bool) f.getProperty ("vendor", false);
                if (e.name.isNotEmpty()) out.push_back (e);
            }
        return out;
    }

    // la plus recente des deux copies : celle du plugin ou celle mise a jour depuis GitHub
    static std::vector<CatalogEntry> load (juce::String& date)
    {
        juce::String bd, cd;
        auto builtin = parse (juce::String::fromUTF8 (BinaryData::firmwares_json, BinaryData::firmwares_jsonSize), &bd);
        if (cacheFile().existsAsFile())
        {
            auto cached = parse (cacheFile().loadFileAsString(), &cd);
            if (! cached.empty() && cd >= bd) { date = cd; return cached; }
        }
        date = bd;
        return builtin;
    }

    static bool download (const juce::String& url, juce::MemoryBlock& out, juce::String& error, std::function<void (float)> progress = {})
    {
        juce::URL u (url);
        int status = 0;
        auto in = u.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                           .withExtraHeaders ("User-Agent: FM-9 (NINE HANDS | DSP)").withConnectionTimeoutMs (20000)
                                           .withStatusCode (&status).withNumRedirectsToFollow (5));
        if (in == nullptr || status >= 400) { error = "download failed (HTTP " + juce::String (status) + ")"; return false; }
        out.reset();
        // par morceaux, pour suivre la progression (taille annoncee par le serveur)
        const auto total = in->getTotalLength();
        juce::HeapBlock<char> chunk (65536);
        for (;;)
        {
            const int n = in->read (chunk, 65536);
            if (n <= 0) break;
            out.append (chunk, (size_t) n);
            if (progress && total > 0) progress (juce::jlimit (0.0f, 1.0f, (float) out.getSize() / (float) total));
        }
        return out.getSize() > 0;
    }

    // installe le paquet materiel (.fwsc) et la version web (.wasm) quand ils existent ; thread d'arriere-plan
    static bool install (const CatalogEntry& e, juce::String& error, std::function<void (float)> progress = {})
    {
        // progression : paquet materiel puis version web (moitie chacun quand les deux existent)
        const float parts = (e.hardware() ? 1.0f : 0.0f) + (e.web() ? 1.0f : 0.0f);
        auto part = [&] (float base) { return [progress, base, parts] (float f) { if (progress) progress ((base + f) / juce::jmax (1.0f, parts)); }; };
        if (! e.compatible) { error = e.reason; return false; }
        FirmwareInstaller::folder().createDirectory();
        int done = 0;
        juce::String errs;
        auto note = [&] (const juce::String& what, const juce::String& src)
        {
            FirmwareInstaller::folder().getChildFile (e.fileName() + ".txt").replaceWithText (
                "name: " + e.name + "\nauthor: " + e.author + (e.repo.isNotEmpty() ? "\nsource: https://github.com/" + e.repo : juce::String())
                + "\n" + what + ": " + src + "\nlicense: " + (e.license.isNotEmpty() ? e.license : juce::String ("see the project")) + "\n");
        };
        if (e.hardware())
        {
            juce::MemoryBlock fw; juce::String err;
            if (! download (e.pkg, fw, err, part (0.0f))) errs << "package: " << err << "  ";
            else if (! FirmwareHost::isHardwarePackage (fw.getData(), fw.getSize())) errs << "package: not an FM-1 update package  ";
            else if (FirmwareInstaller::folder().getChildFile (e.fileName() + ".fwsc").replaceWithData (fw.getData(), fw.getSize())) { ++done; note ("package", e.pkg); }
        }
        if (e.web())
        {
            juce::MemoryBlock wasm; juce::String err;
            if (! download (e.url, wasm, err, part (e.hardware() ? 1.0f : 0.0f))) errs << "web build: " << err;
            else if (FirmwareInstaller::abiOf (wasm.getData(), wasm.getSize()) == 0) errs << "web build: not an FM-1 web build";
            else if (FirmwareHost::create (wasm.getData(), wasm.getSize(), juce::File(), err) == nullptr) errs << "web build: " << err;
            else if (FirmwareInstaller::folder().getChildFile (e.fileName() + ".wasm").replaceWithData (wasm.getData(), wasm.getSize())) { ++done; if (! e.hardware()) note ("file", e.url); }
        }
        if (done == 0) error = errs.trim();
        return done > 0;
    }
};

// ------------------------------------------------------------------------------- fenetre du catalogue
class FirmwareCatalogView : public juce::Component, private juce::Timer
{
public:
    std::function<void (const juce::String&)> onActivate;     // nom du firmware installe a lancer
    // chargement avec progression (bouton Load) : prepare le firmware hors du thread de l'interface, puis l'editeur l'active
    std::function<std::unique_ptr<FirmwareHost> (const juce::String&, juce::String&, std::function<void (float)>)> loader;
    std::function<void (const juce::String&, std::shared_ptr<std::unique_ptr<FirmwareHost>>, const juce::String&)> onLoaded;
    std::function<void()> onClose;

    FirmwareCatalogView (juce::Typeface::Ptr reg, juce::Typeface::Ptr med, juce::Typeface::Ptr bold) : fReg (reg), fMed (med), fBold (bold)
    {
        entries = FirmwareCatalog::load (date);
        installed = FirmwareInstaller::installed();
        addAndMakeVisible (viewport);
        viewport.setViewedComponent (&list, false);
        viewport.setScrollBarsShown (true, false);
        viewport.setScrollBarThickness (8);
        viewport.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, NineHandsMenu::kFaint);
        list.owner = this;
        list.setMouseCursor (juce::MouseCursor::NormalCursor);
        startTimerHz (20);
    }
    ~FirmwareCatalogView() override
    {
        stop = true;
        if (worker.joinable()) worker.join();
        if (previewThread.joinable()) previewThread.join();
        if (loadThread.joinable()) loadThread.join();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (18);
        r.removeFromTop (headerH);
        r.removeFromBottom (footerH);
        viewport.setBounds (r);
        layoutList();
    }

    void paint (juce::Graphics& g) override
    {
        using namespace NineHandsMenu;
        g.fillAll (juce::Colours::black.withAlpha (0.55f));
        auto box = getLocalBounds().reduced (8).toFloat();
        g.setColour (kBg); g.fillRoundedRectangle (box, 12.0f);
        g.setColour (kBorder); g.drawRoundedRectangle (box, 12.0f, 1.0f);
        auto r = getLocalBounds().reduced (18);
        auto head = r.removeFromTop (headerH);
        g.setColour (kText); g.setFont (font (fBold, 20.0f));
        g.drawText ("FM-1 firmware catalog", head.removeFromTop (40), juce::Justification::centredLeft);
        int ok = 0; for (auto& e : entries) ok += e.compatible ? 1 : 0;
        g.setColour (kDim); g.setFont (font (fReg, 13.0f));
        g.drawText (juce::String (ok) + " compatible / " + juce::String ((int) entries.size()) + " firmwares  -  lists: aroum/fm1-custom-fw, fm1-emu"
                    + (date.isNotEmpty() ? "  -  " + date : juce::String()), head.removeFromTop (28), juce::Justification::centredLeft);
        // pied : etat + boutons
        auto foot = getLocalBounds().reduced (18).removeFromBottom (footerH).toFloat();
        g.setColour (status.startsWith ("!") ? juce::Colour (0xffff8a80) : kDim); g.setFont (font (fReg, 13.0f));
        g.drawText (status.trimCharactersAtStart ("!"), foot.withTrimmedRight (560).toNearestInt(), juce::Justification::centredLeft);
        drawButton (g, btnClose (), "Close", false);
        drawButton (g, btnRefresh (), busy ? "..." : "Refresh", false);
        drawButton (g, btnAll (), "Install all compatible", true);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto p = e.getPosition();
        if (btnClose().contains (p)) { if (onClose) onClose(); return; }
        if (busy) return;
        if (btnRefresh().contains (p)) refresh();
        else if (btnAll().contains (p))
        {
            std::vector<CatalogEntry> todo;
            // tous les firmwares libres compatibles (celui de M-VAVE s'installe a part, d'un clic)
            for (auto& x : entries) if (x.compatible && ! x.vendor && ! x.isInstalled (installed)) todo.push_back (x);
            if (todo.empty()) { status = "All compatible firmwares are already installed."; repaint(); return; }
            runInstall (todo);
        }
    }

private:
    static constexpr float textScale = 1.4f;        // textes du catalogue agrandis
    static juce::Font font (const juce::Typeface::Ptr& t, float h) { return juce::Font (juce::FontOptions (t).withHeight (h * textScale)); }
    static constexpr int headerH = 80, footerH = 56, rowH = 82, groupH = 42;
    juce::Rectangle<int> footArea() const { return getLocalBounds().reduced (18).removeFromBottom (footerH).withTrimmedTop (8); }
    juce::Rectangle<int> btnClose() const   { return footArea().removeFromRight (110); }
    juce::Rectangle<int> btnRefresh() const { auto a = footArea(); a.removeFromRight (122); return a.removeFromRight (120); }
    juce::Rectangle<int> btnAll() const     { auto a = footArea(); a.removeFromRight (254); return a.removeFromRight (290); }

    void drawButton (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& t, bool primary) const
    {
        using namespace NineHandsMenu;
        const bool dim = busy && t != "Close";
        g.setColour (primary ? kAccent.withAlpha (dim ? 0.25f : 0.9f) : kHover); g.fillRoundedRectangle (r.toFloat(), 7.0f);
        if (! primary) { g.setColour (kBorder); g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 7.0f, 1.0f); }
        g.setColour (primary ? juce::Colour (0xff0b1520) : (dim ? kFaint : kText)); g.setFont (font (fMed, 13.5f));
        g.drawText (t, r, juce::Justification::centred);
    }

    struct Row { int entry = -1; juce::String group; };
    std::vector<Row> rows;

    void layoutList()
    {
        rows.clear();
        juce::String g;
        // compatibles d'abord dans chaque famille
        std::vector<int> idx ((size_t) entries.size());
        for (size_t i = 0; i < idx.size(); ++i) idx[i] = (int) i;
        std::stable_sort (idx.begin(), idx.end(), [this] (int a, int b) { return entries[(size_t) a].compatible && ! entries[(size_t) b].compatible; });
        juce::StringArray groups;
        for (auto& e : entries) groups.addIfNotAlreadyThere (e.group);
        for (auto& grp : groups)
        {
            rows.push_back ({ -1, grp });
            for (int i : idx) if (entries[(size_t) i].group == grp) rows.push_back ({ i, {} });
        }
        int h = 0;
        for (auto& r : rows) h += r.entry < 0 ? groupH : rowH;
        list.setSize (juce::jmax (100, viewport.getWidth() - 10), h);
        list.repaint();
    }

    juce::Rectangle<int> installRect (juce::Rectangle<int> row) const { return row.removeFromRight (140).withSizeKeepingCentre (128, 38); }

    struct List : juce::Component
    {
        FirmwareCatalogView* owner = nullptr;
        void paint (juce::Graphics& g) override
        {
            using namespace NineHandsMenu;
            auto& o = *owner;
            int y = 0;
            for (auto& r : o.rows)
            {
                if (r.entry < 0)
                {
                    g.setColour (kDim); g.setFont (font (o.fBold, 11.5f).withExtraKerningFactor (0.14f));
                    g.drawText (r.group.toUpperCase(), juce::Rectangle<int> (6, y + 14, getWidth() - 12, 26), juce::Justification::centredLeft);
                    y += groupH; continue;
                }
                const auto& e = o.entries[(size_t) r.entry];
                auto row = juce::Rectangle<int> (0, y, getWidth(), rowH - 4);
                if (r.entry == o.hover) { g.setColour (kHover); g.fillRoundedRectangle (row.toFloat(), 7.0f); }
                auto inner = row.reduced (8, 6);
                auto btn = o.installRect (row);
                // statut : texte seul, vert compatible / rouge non compatible, et le type dessous
                auto badge = inner.removeFromRight (320).removeFromLeft (172).toFloat();
                g.setFont (font (o.fMed, 13.5f));
                g.setColour (e.compatible ? juce::Colour (0xff5be08a) : juce::Colour (0xffff5a52));
                g.drawText (e.compatible ? "Compatible" : "Not compatible", badge.withHeight (28.0f).withY (badge.getCentreY() - (e.compatible ? 21.0f : 14.0f)).toNearestInt(), juce::Justification::centred);
                if (e.compatible)
                {
                    const auto kind = e.hardware() && e.web() ? juce::String ("HW + WEB") : e.hardware() ? juce::String ("HARDWARE") : juce::String ("WEB");
                    g.setColour (kDim); g.setFont (font (o.fBold, 10.0f).withExtraKerningFactor (0.1f));
                    g.drawText (kind, badge.withHeight (20.0f).withY (badge.getCentreY() + 6.0f).toNearestInt(), juce::Justification::centred);
                }
                // nom, auteur, description ou raison
                g.setColour (e.compatible ? kText : kText.withAlpha (0.6f)); g.setFont (font (o.fMed, 15.0f));
                g.drawText (e.name + (e.author.isNotEmpty() ? "" : ""), inner.removeFromTop (30), juce::Justification::centredLeft);
                g.setColour (kDim); g.setFont (font (o.fReg, 12.5f));
                juce::String sub = (e.author.isNotEmpty() ? "by " + e.author + "  -  " : juce::String())
                                   + (e.vendor ? juce::String ("M-VAVE's own firmware, downloaded from M-VAVE  -  ") : juce::String()) + (e.compatible ? e.desc : e.reason);
                g.drawFittedText (sub, inner, juce::Justification::topLeft, 1, 0.9f);
                // bouton
                if (e.compatible)
                {
                    const bool inst = e.isInstalled (o.installed);
                    const bool doing = o.current == r.entry;
                    const bool loading = o.loadingIdx == r.entry;
                    g.setColour (inst ? kHover : kAccent.withAlpha (o.busy ? 0.3f : 0.9f)); g.fillRoundedRectangle (btn.toFloat(), 7.0f);
                    if (inst) { g.setColour (kBorder); g.drawRoundedRectangle (btn.toFloat().reduced (0.5f), 7.0f, 1.0f); }
                    if (doing || loading)
                    {
                        // telechargement (Install) ou demarrage du firmware (Load) en cours : le bouton se remplit
                        const float f = loading ? o.loadProg.load() : o.installProg.load();
                        g.setColour (kHover); g.fillRoundedRectangle (btn.toFloat(), 7.0f);
                        g.setColour (kAccent); g.fillRoundedRectangle (btn.toFloat().withWidth (juce::jmax (14.0f, btn.getWidth() * f)), 7.0f);
                        g.setColour (kText); g.setFont (font (o.fMed, 13.0f));
                        g.drawText (juce::String (juce::roundToInt (f * 100.0f)) + " %", btn, juce::Justification::centred);
                    }
                    else
                    {
                        g.setColour (inst ? kText : juce::Colour (0xff0b1520)); g.setFont (font (o.fMed, 13.0f));
                        g.drawText (inst ? "Load" : "Install", btn, juce::Justification::centred);
                    }
                }
                y += rowH;
            }
        }
        int entryAt (juce::Point<int> p, juce::Rectangle<int>* rowOut = nullptr) const
        {
            int y = 0;
            for (auto& r : owner->rows)
            {
                const int h = r.entry < 0 ? groupH : rowH;
                if (p.y >= y && p.y < y + h) { if (rowOut) *rowOut = { 0, y, getWidth(), rowH - 4 }; return r.entry; }
                y += h;
            }
            return -1;
        }
        void mouseMove (const juce::MouseEvent& e) override
        {
            juce::Rectangle<int> row;
            const int i = entryAt (e.getPosition(), &row);
            if (i != owner->hover) { owner->hover = i; repaint(); }
            // apercu de l'ecran : uniquement sur le nom du firmware
            int name = -1;
            if (i >= 0)
            {
                const auto& en = owner->entries[(size_t) i];
                const auto nameArea = row.reduced (8, 6).removeFromTop (30)
                                         .withWidth (juce::roundToInt (juce::GlyphArrangement::getStringWidth (font (owner->fMed, 15.0f), en.name)) + 6);
                if (nameArea.contains (e.getPosition()) && en.compatible) name = i;
            }
            owner->hoverName (name, owner->getLocalPoint (this, e.getPosition()));
        }
        void mouseExit (const juce::MouseEvent&) override { owner->hover = -1; owner->hoverName (-1, {}); repaint(); }
        void mouseDown (const juce::MouseEvent& ev) override
        {
            juce::Rectangle<int> row;
            const int i = entryAt (ev.getPosition(), &row);
            if (i < 0) return;
            const auto& e = owner->entries[(size_t) i];
            if (! owner->installRect (row).contains (ev.getPosition()))
            {
                // clic sur la ligne : page du projet
                const auto link = e.page.isNotEmpty() ? e.page : (e.repo.isNotEmpty() ? "https://github.com/" + e.repo : juce::String());
                if (link.isNotEmpty() && ev.getNumberOfClicks() > 1) juce::URL (link).launchInDefaultBrowser();
                return;
            }
            if (! e.compatible || owner->busy) return;
            if (e.isInstalled (owner->installed)) { owner->startLoad (i, e.loadName (owner->installed)); return; }
            owner->runInstall ({ e });
        }
    } list;

    void startLoad (int idx, const juce::String& name)
    {
        if (loader == nullptr) { if (onActivate) onActivate (name); return; }
        if (loadingIdx >= 0) return;
        if (loadThread.joinable()) loadThread.join();
        loadingIdx = idx; loadProg.store (0.0f); busy = true;
        status = "Starting " + entries[(size_t) idx].name + "...";
        const juce::Component::SafePointer<FirmwareCatalogView> self (this);
        auto load = loader; auto done = onLoaded;
        loadThread = std::thread ([this, self, name, load, done]
        {
            juce::String err;
            auto fw = load (name, err, [this] (float f) { loadProg.store (f); });
            auto holder = std::make_shared<std::unique_ptr<FirmwareHost>> (std::move (fw));
            juce::MessageManager::callAsync ([self, name, holder, err, done]
            {
                if (self != nullptr) { self->loadingIdx = -1; self->busy = false; self->status.clear(); }
                if (done) done (name, holder, err);
            });
        });
        repaint();
    }

    void runInstall (std::vector<CatalogEntry> todo)
    {
        busy = true; done = 0; total = (int) todo.size(); failures.clear();
        if (worker.joinable()) worker.join();
        worker = std::thread ([this, todo]
        {
            for (auto& e : todo)
            {
                if (stop) break;
                { const juce::ScopedLock sl (lock); progress = "Installing " + e.name + " (" + juce::String (done.load() + 1) + "/" + juce::String (total) + ")..."; currentName = e.name; }
                juce::String err;
                installProg.store (0.0f);
                if (! FirmwareCatalog::install (e, err, [this] (float f) { installProg.store (f); })) { const juce::ScopedLock sl (lock); failures.add (e.name + ": " + err); }
                ++done;
            }
            finished = true;
        });
        repaint();
    }

    void refresh()
    {
        busy = true; status = "Downloading the latest catalog...";
        if (worker.joinable()) worker.join();
        worker = std::thread ([this]
        {
            juce::URL u (FirmwareCatalog::remoteUrl);
            int st = 0;
            juce::String txt;
            if (auto in = u.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress).withConnectionTimeoutMs (15000).withStatusCode (&st)))
                txt = in->readEntireStreamAsString();
            const juce::ScopedLock sl (lock);
            if (st < 400 && ! FirmwareCatalog::parse (txt).empty())
            {
                FirmwareInstaller::folder().createDirectory();
                FirmwareCatalog::cacheFile().replaceWithText (txt);
                refreshed = 1;
            }
            else refreshed = -1;
            finished = true;
        });
        repaint();
    }

    void timerCallback() override
    {
        tickPreview();
        if (previewBusy.load() && shown >= 0) repaint();
        if (loadingIdx >= 0) { list.repaint(); return; }
        if (! busy) return;
        {
            const juce::ScopedLock sl (lock);
            if (progress.isNotEmpty()) { status = progress; progress.clear(); for (size_t i = 0; i < entries.size(); ++i) if (entries[i].name == currentName) current = (int) i; }
        }
        if (finished.exchange (false))
        {
            if (worker.joinable()) worker.join();
            busy = false; current = -1;
            installed = FirmwareInstaller::installed();
            if (refreshed.load() != 0)
            {
                if (refreshed.load() > 0)
                {
                    entries = FirmwareCatalog::load (date); layoutList(); status = "Catalog updated.";
                    const juce::ScopedLock sl (previewLock); previews.clear(); previewFailed.clear(); shown = nameHover = -1;
                }
                else status = "!Could not download the catalog (offline?)";
                refreshed = 0;
            }
            else if (failures.isEmpty()) status = juce::String (total) + (total > 1 ? " firmwares installed." : " firmware installed.") + " Right-click > Firmware to switch.";
            else status = "!" + juce::String (total - failures.size()) + " installed, " + juce::String (failures.size()) + " failed: " + failures[0];
        }
        repaint(); list.repaint();
    }

    // ---------------------------------------------------------------- apercu de l'ecran au survol (800 ms)
public:
    void hoverName (int entry, juce::Point<int> at)
    {
        if (entry != nameHover) { nameHover = entry; nameSince = juce::Time::getMillisecondCounterHiRes(); shown = -1; repaint(); }
        if (entry >= 0) previewAt = at;
    }
    void paintOverChildren (juce::Graphics& g) override
    {
        using namespace NineHandsMenu;
        if (shown < 0) return;
        // vignette : l'ecran de la FM-1 (240 x 240) dans un cadre sombre, pres du curseur, toujours dans la fenetre
        const int sz = 196, pad = 10;
        juce::Rectangle<int> box (previewAt.x + 18, previewAt.y - sz / 2 - pad, sz + 2 * pad, sz + 2 * pad + 22);
        box = box.constrainedWithin (getLocalBounds().reduced (12));
        g.setColour (juce::Colours::black.withAlpha (0.35f)); g.fillRoundedRectangle (box.toFloat().translated (3, 4), 12.0f);
        g.setColour (kBg); g.fillRoundedRectangle (box.toFloat(), 12.0f);
        g.setColour (kBorder); g.drawRoundedRectangle (box.toFloat().reduced (0.5f), 12.0f, 1.0f);
        auto scr = juce::Rectangle<int> (box.getX() + pad, box.getY() + pad, sz, sz);
        g.setColour (juce::Colour (0xff06080b)); g.fillRoundedRectangle (scr.toFloat(), 6.0f);
        juce::Image img;
        {
            const juce::ScopedLock sl (previewLock);
            auto it = previews.find (shown);
            if (it != previews.end()) img = it->second;
        }
        g.setFont (font (fReg, 12.0f));
        if (img.isValid())
        {
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.drawImage (img, scr.toFloat());
        }
        else if (previewFailed.count (shown))
        {
            g.setColour (kDim);
            g.drawText ("No preview", scr, juce::Justification::centred);
        }
        else
        {
            // progression : telechargement (moitie gauche) puis demarrage du firmware (moitie droite)
            const int st = previewStage.load();
            const float f = juce::jlimit (0.0f, 1.0f, (st == 0 ? 0.0f : 0.5f) + 0.5f * previewProg.load());
            g.setColour (kText);
            g.drawText (st == 0 ? "Downloading..." : "Starting the firmware...", scr.withTrimmedBottom (sz / 2 - 6), juce::Justification::centredBottom);
            auto bar = juce::Rectangle<float> ((float) scr.getX() + 24.0f, (float) scr.getCentreY() + 8.0f, (float) sz - 48.0f, 6.0f);
            g.setColour (kFaint); g.fillRoundedRectangle (bar, 3.0f);
            g.setColour (kAccent); g.fillRoundedRectangle (bar.withWidth (juce::jmax (6.0f, bar.getWidth() * f)), 3.0f);
            g.setColour (kDim);
            g.drawText (juce::String (juce::roundToInt (f * 100.0f)) + " %", bar.translated (0, 10.0f).withHeight (16.0f).toNearestInt(), juce::Justification::centred);
        }
        g.setColour (kDim);
        g.drawText (entries[(size_t) shown].name, juce::Rectangle<int> (box.getX() + pad, scr.getBottom() + 4, sz, 16), juce::Justification::centredLeft);
    }

    // capture de la notice : apercu d'un firmware calcule tout de suite, affiche au point donne
    void showPreviewNow (const juce::String& name, juce::Point<int> at)
    {
        for (size_t i = 0; i < entries.size(); ++i)
            if (entries[i].name == name)
            {
                auto img = makePreview (entries[i]);
                const juce::ScopedLock sl (previewLock);
                if (img.isValid()) previews[(int) i] = img; else previewFailed.insert ((int) i);
                nameHover = shown = (int) i; previewAt = at; hover = (int) i;
                list.repaint(); repaint();
                return;
            }
    }

    // capture de la notice : etat de chargement fige (apercu et installation en cours)
    void showLoadingForTest (const juce::String& previewName, int stage, float f, juce::Point<int> at, const juce::String& installName, float inst)
    {
        for (size_t i = 0; i < entries.size(); ++i)
        {
            if (entries[i].name == previewName) { nameHover = shown = (int) i; previewAt = at; previewBusy.store (true); previewStage.store (stage); previewProg.store (f); }
            if (entries[i].name == installName) { current = (int) i; busy = true; installProg.store (inst); status = "Installing " + installName + " (1/1)..."; }
            if ("!" + entries[i].name == installName) { loadingIdx = (int) i; busy = true; loadProg.store (inst); status = "Starting " + entries[i].name + "..."; }
        }
        list.repaint(); repaint();
    }

    static juce::File previewFile (const CatalogEntry& e) { return FirmwareInstaller::folder().getChildFile ("Previews").getChildFile (e.fileName() + ".png"); }

    // l'apercu d'un firmware : fichier en cache, sinon le firmware (installe ou telecharge) demarre hors ecran
    static juce::Image makePreview (const CatalogEntry& e, std::function<void (int, float)> progress = {})
    {
        auto stage = [progress] (int st) { return [progress, st] (float f) { if (progress) progress (st, f); }; };
        const auto cache = previewFile (e);
        if (cache.existsAsFile()) if (auto img = juce::ImageFileFormat::loadFrom (cache); img.isValid()) return img;
        juce::MemoryBlock data; juce::String err;
        const auto folder = FirmwareInstaller::folder();
        if (folder.getChildFile (e.fileName() + ".fwsc").loadFileAsData (data) || folder.getChildFile (e.fileName() + ".wasm").loadFileAsData (data)) {}
        else if (e.web()) FirmwareCatalog::download (e.url, data, err, stage (0));          // la version web est plus legere a telecharger
        else if (e.hardware()) FirmwareCatalog::download (e.pkg, data, err, stage (0));
        if (data.getSize() == 0) return {};
        auto img = FirmwareHost::renderPreview (data.getData(), data.getSize(), 3.0, stage (1));
        if (img.isValid())
        {
            cache.getParentDirectory().createDirectory();
            juce::FileOutputStream os (cache);
            if (os.openedOk()) { os.setPosition (0); os.truncate(); juce::PNGImageFormat().writeImageToStream (img, os); }
        }
        return img;
    }

private:
    void tickPreview()
    {
        if (nameHover < 0) { if (shown >= 0) { shown = -1; repaint(); } return; }
        if (shown == nameHover || juce::Time::getMillisecondCounterHiRes() - nameSince < 800.0) return;
        shown = nameHover;
        repaint();
        {
            const juce::ScopedLock sl (previewLock);
            if (previews.count (shown) || previewFailed.count (shown) || pending.count (shown)) return;
            pending.insert (shown);
        }
        // un seul firmware demarre a la fois, hors du thread de l'interface
        const int idx = shown;
        const auto e = entries[(size_t) idx];
        if (previewThread.joinable()) { if (previewBusy.load()) { const juce::ScopedLock sl (previewLock); pending.erase (idx); shown = -1; nameSince = juce::Time::getMillisecondCounterHiRes(); return; } previewThread.join(); }
        previewBusy.store (true);
        const juce::Component::SafePointer<FirmwareCatalogView> self (this);
        previewThread = std::thread ([this, self, idx, e]
        {
            previewStage.store (0); previewProg.store (0.0f);
            auto img = makePreview (e, [this] (int st, float f) { previewStage.store (st); previewProg.store (f); });
            {
                const juce::ScopedLock sl (previewLock);
                pending.erase (idx);
                if (img.isValid()) previews[idx] = img; else previewFailed.insert (idx);
            }
            previewBusy.store (false);
            juce::MessageManager::callAsync ([self] { if (self != nullptr) self->repaint(); });
        });
    }

    int nameHover = -1, shown = -1;
    double nameSince = 0;
    juce::Point<int> previewAt;
    juce::CriticalSection previewLock;
    std::map<int, juce::Image> previews;
    std::set<int> previewFailed, pending;
    std::thread previewThread;
    std::atomic<bool> previewBusy { false };
    std::atomic<int> previewStage { 0 };       // 0 telechargement, 1 demarrage du firmware
    std::atomic<float> previewProg { 0.0f };

    juce::Typeface::Ptr fReg, fMed, fBold;
    std::vector<CatalogEntry> entries;
    juce::String date, status, progress, currentName;
    juce::StringArray installed, failures;
    juce::Viewport viewport;
    juce::CriticalSection lock;
    std::thread worker;
    std::atomic<bool> stop { false }, finished { false };
    int hover = -1, current = -1, total = 0;
    std::atomic<int> done { 0 }, refreshed { 0 };
    std::atomic<float> installProg { 0.0f }, loadProg { 0.0f };
    int loadingIdx = -1;
    std::thread loadThread;
    bool busy = false;
};
} // namespace fm9
