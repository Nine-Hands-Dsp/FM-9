// FM-9 (NINE HANDS | DSP) - catalogue des firmwares de la FM-1.
// Liste tiree du hub github.com/aroum/fm1-custom-fw par tools/gen_fw_catalog.py : chaque firmware est
// marque compatible (version web .wasm qui demarre dans FM-9) ou non (avec la raison).
// Copie integree au plugin, mise a jour depuis le depot de FM-9 (bouton Refresh).
#pragma once
#include <JuceHeader.h>
#include "FirmwareHost.h"
#include "NineHandsMenu.h"

namespace fm9
{
struct CatalogEntry
{
    juce::String name, author, group, desc, repo, page, reason, url, abi, pkg, license;
    bool compatible = false, vendor = false;
    juce::int64 size = 0;
    // nom des fichiers installes : le depot, sinon le nom du firmware
    juce::String fileName() const
    {
        const auto base = repo.isNotEmpty() ? repo.fromFirstOccurrenceOf ("/", false, false) : name;
        return juce::File::createLegalFileName (base.isNotEmpty() ? base : name);
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

    static bool download (const juce::String& url, juce::MemoryBlock& out, juce::String& error)
    {
        juce::URL u (url);
        int status = 0;
        auto in = u.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                           .withExtraHeaders ("User-Agent: FM-9 (NINE HANDS | DSP)").withConnectionTimeoutMs (20000)
                                           .withStatusCode (&status).withNumRedirectsToFollow (5));
        if (in == nullptr || status >= 400) { error = "download failed (HTTP " + juce::String (status) + ")"; return false; }
        out.reset();
        in->readIntoMemoryBlock (out);
        return out.getSize() > 0;
    }

    // installe le paquet materiel (.fwsc) et la version web (.wasm) quand ils existent ; thread d'arriere-plan
    static bool install (const CatalogEntry& e, juce::String& error)
    {
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
            if (! download (e.pkg, fw, err)) errs << "package: " << err << "  ";
            else if (! FirmwareHost::isHardwarePackage (fw.getData(), fw.getSize())) errs << "package: not an FM-1 update package  ";
            else if (FirmwareInstaller::folder().getChildFile (e.fileName() + ".fwsc").replaceWithData (fw.getData(), fw.getSize())) { ++done; note ("package", e.pkg); }
        }
        if (e.web())
        {
            juce::MemoryBlock wasm; juce::String err;
            if (! download (e.url, wasm, err)) errs << "web build: " << err;
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
        startTimerHz (10);
    }
    ~FirmwareCatalogView() override { stop = true; if (worker.joinable()) worker.join(); }

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
        g.drawText ("FM-1 firmware catalog", head.removeFromTop (28), juce::Justification::centredLeft);
        int ok = 0; for (auto& e : entries) ok += e.compatible ? 1 : 0;
        g.setColour (kDim); g.setFont (font (fReg, 13.0f));
        g.drawText (juce::String (ok) + " compatible / " + juce::String ((int) entries.size()) + " firmwares  -  lists: aroum/fm1-custom-fw, fm1-emu"
                    + (date.isNotEmpty() ? "  -  " + date : juce::String()), head.removeFromTop (20), juce::Justification::centredLeft);
        // pied : etat + boutons
        auto foot = getLocalBounds().reduced (18).removeFromBottom (footerH).toFloat();
        g.setColour (status.startsWith ("!") ? juce::Colour (0xffff8a80) : kDim); g.setFont (font (fReg, 13.0f));
        g.drawText (status.trimCharactersAtStart ("!"), foot.withTrimmedRight (420).toNearestInt(), juce::Justification::centredLeft);
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
    static juce::Font font (const juce::Typeface::Ptr& t, float h) { return juce::Font (juce::FontOptions (t).withHeight (h)); }
    static constexpr int headerH = 56, footerH = 40, rowH = 58;
    juce::Rectangle<int> footArea() const { return getLocalBounds().reduced (18).removeFromBottom (footerH).withTrimmedTop (8); }
    juce::Rectangle<int> btnClose() const   { return footArea().removeFromRight (80); }
    juce::Rectangle<int> btnRefresh() const { auto a = footArea(); a.removeFromRight (90); return a.removeFromRight (90); }
    juce::Rectangle<int> btnAll() const     { auto a = footArea(); a.removeFromRight (190); return a.removeFromRight (200); }

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
        for (auto& r : rows) h += r.entry < 0 ? 30 : rowH;
        list.setSize (juce::jmax (100, viewport.getWidth() - 10), h);
        list.repaint();
    }

    juce::Rectangle<int> installRect (juce::Rectangle<int> row) const { return row.removeFromRight (104).withSizeKeepingCentre (96, 28); }

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
                    g.drawText (r.group.toUpperCase(), juce::Rectangle<int> (6, y + 10, getWidth() - 12, 18), juce::Justification::centredLeft);
                    y += 30; continue;
                }
                const auto& e = o.entries[(size_t) r.entry];
                auto row = juce::Rectangle<int> (0, y, getWidth(), rowH - 4);
                if (r.entry == o.hover) { g.setColour (kHover); g.fillRoundedRectangle (row.toFloat(), 7.0f); }
                auto inner = row.reduced (8, 6);
                auto btn = o.installRect (row);
                // badge
                auto badge = inner.removeFromRight (240).removeFromLeft (128).withSizeKeepingCentre (122, 20).toFloat();
                if (e.compatible) badge = badge.translated (0.0f, -7.0f);
                const auto col = e.compatible ? juce::Colour (0xff5be08a) : juce::Colour (0xff8d96a4);
                g.setColour (col.withAlpha (0.16f)); g.fillRoundedRectangle (badge, 10.0f);
                g.setColour (col); g.setFont (font (o.fMed, 12.0f));
                g.drawText (e.compatible ? "Compatible" : "Not compatible", badge.toNearestInt(), juce::Justification::centred);
                if (e.compatible)
                {
                    // type : paquet materiel (le vrai firmware, emule) et / ou version web
                    const auto kind = e.hardware() && e.web() ? juce::String ("HW + WEB") : e.hardware() ? juce::String ("HARDWARE") : juce::String ("WEB");
                    g.setColour (kDim); g.setFont (font (o.fBold, 10.0f).withExtraKerningFactor (0.1f));
                    g.drawText (kind, badge.translated (0.0f, 17.0f).toNearestInt(), juce::Justification::centred);
                }
                // nom, auteur, description ou raison
                g.setColour (e.compatible ? kText : kText.withAlpha (0.6f)); g.setFont (font (o.fMed, 15.0f));
                g.drawText (e.name + (e.author.isNotEmpty() ? "" : ""), inner.removeFromTop (20), juce::Justification::centredLeft);
                g.setColour (kDim); g.setFont (font (o.fReg, 12.5f));
                juce::String sub = (e.author.isNotEmpty() ? "by " + e.author + "  -  " : juce::String())
                                   + (e.vendor ? juce::String ("M-VAVE's own firmware, downloaded from M-VAVE  -  ") : juce::String()) + (e.compatible ? e.desc : e.reason);
                g.drawFittedText (sub, inner, juce::Justification::topLeft, 1, 0.9f);
                // bouton
                if (e.compatible)
                {
                    const bool inst = e.isInstalled (o.installed);
                    const bool doing = o.current == r.entry;
                    g.setColour (inst ? kHover : kAccent.withAlpha (o.busy ? 0.3f : 0.9f)); g.fillRoundedRectangle (btn.toFloat(), 7.0f);
                    if (inst) { g.setColour (kBorder); g.drawRoundedRectangle (btn.toFloat().reduced (0.5f), 7.0f, 1.0f); }
                    g.setColour (inst ? kText : juce::Colour (0xff0b1520)); g.setFont (font (o.fMed, 13.0f));
                    g.drawText (doing ? "..." : inst ? "Load" : "Install", btn, juce::Justification::centred);
                }
                y += rowH;
            }
        }
        int entryAt (juce::Point<int> p, juce::Rectangle<int>* rowOut = nullptr) const
        {
            int y = 0;
            for (auto& r : owner->rows)
            {
                const int h = r.entry < 0 ? 30 : rowH;
                if (p.y >= y && p.y < y + h) { if (rowOut) *rowOut = { 0, y, getWidth(), rowH - 4 }; return r.entry; }
                y += h;
            }
            return -1;
        }
        void mouseMove (const juce::MouseEvent& e) override { const int i = entryAt (e.getPosition()); if (i != owner->hover) { owner->hover = i; repaint(); } }
        void mouseExit (const juce::MouseEvent&) override { owner->hover = -1; repaint(); }
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
            if (e.isInstalled (owner->installed)) { if (owner->onActivate) owner->onActivate (e.loadName (owner->installed)); return; }
            owner->runInstall ({ e });
        }
    } list;

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
                if (! FirmwareCatalog::install (e, err)) { const juce::ScopedLock sl (lock); failures.add (e.name + ": " + err); }
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
                if (refreshed.load() > 0) { entries = FirmwareCatalog::load (date); layoutList(); status = "Catalog updated."; }
                else status = "!Could not download the catalog (offline?)";
                refreshed = 0;
            }
            else if (failures.isEmpty()) status = juce::String (total) + (total > 1 ? " firmwares installed." : " firmware installed.") + " Right-click > Firmware to switch.";
            else status = "!" + juce::String (total - failures.size()) + " installed, " + juce::String (failures.size()) + " failed: " + failures[0];
        }
        repaint(); list.repaint();
    }

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
    bool busy = false;
};
} // namespace fm9
