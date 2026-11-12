// FM-9 (NINE HANDS | DSP) - catalogue des firmwares de la FM-1.
// Liste tiree du hub github.com/aroum/fm1-custom-fw par tools/gen_fw_catalog.py : chaque firmware est
// marque compatible (version web .wasm qui demarre dans FM-9) ou non (avec la raison).
// Copie integree au plugin, mise a jour depuis le depot de FM-9 (bouton Refresh).
#pragma once
#include <cstring>
#include <cctype>
#include <JuceHeader.h>
#include "FirmwareHost.h"
#include "NineHandsMenu.h"
#include <array>
#include <map>
#include <set>
#include <thread>

namespace fm9
{
struct CatalogEntry
{
    juce::String name, author, group, desc, repo, page, reason, url, abi, pkg, license, pkgVersion, added;   // added : date d'ajout sur thefirmwarehub.com
    bool compatible = false, vendor = false, preferWeb = false;   // preferWeb : la version native (web) est plus legere que la puce emulee
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
        if (preferWeb && web()) return inst.contains (fileName());                 // il faut la version native
        return inst.contains (fileName()) || inst.contains (fileName() + FirmwareInstaller::hardwareSuffix);
    }
    // ce que Load lance : le paquet materiel (le vrai firmware) s'il est installe, sinon la version web
    // une version plus recente est au catalogue : le paquet installe ne vient pas de la meme adresse
    bool needsUpdate (const juce::StringArray& inst) const
    {
        if (! hardware() || ! inst.contains (fileName() + FirmwareInstaller::hardwareSuffix)) return false;
        const auto txt = FirmwareInstaller::folder().getChildFile (fileName() + ".txt").loadFileAsString();
        const auto line = txt.fromFirstOccurrenceOf ("package: ", false, false).upToFirstOccurrenceOf ("\n", false, false).trim();
        return line.startsWith ("http") && line != pkg;      // installe depuis le catalogue, autre adresse = autre version
    }

    juce::String loadName (const juce::StringArray& inst) const
    {
        if (preferWeb && inst.contains (fileName())) return fileName();
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
                e.pkgVersion = f.getProperty ("pkgVersion", {}).toString();
                e.added = f.getProperty ("added", {}).toString();
                e.added = f.getProperty ("added", {}).toString();
                e.preferWeb = f.getProperty ("prefer", {}).toString() == "web";
                if (e.name.isNotEmpty()) out.push_back (e);
            }
        return out;
    }

    // la plus recente des deux copies : celle du plugin ou celle mise a jour depuis GitHub
    static std::vector<CatalogEntry> load (juce::String& date)
    {
        juce::String bd, cd;
        auto list = parse (juce::String::fromUTF8 (BinaryData::firmwares_json, BinaryData::firmwares_jsonSize), &bd);
        date = bd;
        if (cacheFile().existsAsFile())
        {
            auto cached = parse (cacheFile().loadFileAsString(), &cd);
            if (! cached.empty() && cd >= bd) { date = cd; list = std::move (cached); }
        }
        // firmwares installes depuis un lien (fiches creees par FM-9), puis listes ajoutees par l'utilisateur, sans doublons
        if (myFile().existsAsFile()) merge (list, parse (myFile().loadFileAsString()));
        for (auto& url : userLists())
            if (auto f = userListCache (url); f.existsAsFile())
                merge (list, parseAny (f.loadFileAsString(), url));
        return list;
    }

    // ---- firmwares installes depuis un lien : fiches gardees dans "My firmwares.json" (format du catalogue)
    static juce::File myFile() { return FirmwareInstaller::folder().getChildFile ("My firmwares.json"); }
    static void addMine (const CatalogEntry& e)
    {
        auto v = juce::JSON::parse (myFile().loadFileAsString());
        juce::Array<juce::var> arr;
        if (auto* a = v.getProperty ("firmwares", {}).getArray()) for (auto& x : *a) if (! x.getProperty ("name", {}).toString().equalsIgnoreCase (e.name)) arr.add (x);
        auto* o = new juce::DynamicObject();
        o->setProperty ("name", e.name); o->setProperty ("author", e.author); o->setProperty ("group", "Installed from a link");
        o->setProperty ("desc", e.desc); o->setProperty ("repo", e.repo); o->setProperty ("page", e.page);
        o->setProperty ("compatible", true); o->setProperty ("reason", ""); o->setProperty ("url", e.url); o->setProperty ("abi", e.url.isNotEmpty() ? "web" : "");
        o->setProperty ("pkg", e.pkg); o->setProperty ("pkgVersion", e.pkgVersion); o->setProperty ("license", e.license); o->setProperty ("added", e.added);
        arr.add (juce::var (o));
        auto* root = new juce::DynamicObject(); root->setProperty ("generated", "mine"); root->setProperty ("firmwares", arr);
        FirmwareInstaller::folder().createDirectory();
        myFile().replaceWithText (juce::JSON::toString (juce::var (root)));
    }

    // installation depuis un lien (installeur web github.io, page github.com, owner/repo) :
    // 1. firmware deja au catalogue (meme depot) : installe comme depuis sa fiche
    // 2. sinon derniere Release GitHub (.fwsc, sinon .wasm) : installe et cree sa fiche (nom, auteur, description du depot, page)
    // rend le nom a lancer, vide si echec
    static juce::String installFromLink (const juce::String& link, juce::String& error, std::function<void (float)> progress = {})
    {
        juce::String owner, repo;
        if (! FirmwareInstaller::parseRepo (link, owner, repo)) { error = "Not a GitHub link."; return {}; }
        const auto full = owner + "/" + repo;
        juce::String date;
        auto all = load (date);
        for (auto& e : all)
            if (e.compatible && e.repo.equalsIgnoreCase (full) && (e.hardware() || e.web()))
            {
                if (! install (e, error, progress)) return {};
                return e.loadName (FirmwareInstaller::installed());
            }
        // depot inconnu du catalogue : sa derniere Release
        juce::MemoryBlock mb; juce::String err;
        CatalogEntry e;
        e.repo = full; e.author = owner; e.page = link.trim(); e.compatible = true;
        e.added = juce::Time::getCurrentTime().formatted ("%Y-%m-%d");
        if (download ("https://api.github.com/repos/" + full, mb, err))
        {
            const auto v = juce::JSON::parse (mb.toString());
            e.desc = v.getProperty ("description", {}).toString();
            if (auto* lic = v.getProperty ("license", {}).getDynamicObject()) e.license = lic->getProperty ("spdx_id").toString();
            e.name = v.getProperty ("name", repo).toString();
        }
        if (e.name.isEmpty()) e.name = repo;
        mb.reset();
        if (download ("https://api.github.com/repos/" + full + "/releases/latest", mb, err))
        {
            const auto v = juce::JSON::parse (mb.toString());
            e.pkgVersion = v.getProperty ("tag_name", {}).toString().trimCharactersAtStart ("vV");
            if (auto* assets = v.getProperty ("assets", {}).getArray())
                for (auto& a : *assets)
                {
                    const auto n = a.getProperty ("name", {}).toString().toLowerCase();
                    const auto u = a.getProperty ("browser_download_url", {}).toString();
                    if (n.endsWith (".fwsc") && ! n.startsWith ("fm-1") && ! n.contains ("stock") && ! n.contains ("original") && e.pkg.isEmpty()) e.pkg = u;
                    else if (n.endsWith (".wasm") && e.url.isEmpty()) e.url = u;
                }
        }
        if (e.pkg.isNotEmpty() || e.url.isNotEmpty())
        {
            if (! install (e, error, progress)) return {};
            addMine (e);
            return e.loadName (FirmwareInstaller::installed());
        }
        // pas de Release : recherche dans les fichiers du depot (branche gh-pages, puis par defaut)
        const auto f = FirmwareInstaller::install (owner, repo, error);
        if (f == juce::File()) return {};
        e.name = f.getFileNameWithoutExtension();
        addMine (e);
        return e.name + (f.hasFileExtension ("fwsc") ? FirmwareInstaller::hardwareSuffix : juce::String());
    }

    // ---- listes de firmwares ajoutees (clic droit > Add > Add a firmware list) : un lien par ligne
    static juce::File userListsFile() { return FirmwareInstaller::folder().getChildFile ("Firmware lists.txt"); }
    static constexpr const char* hubList = "https://thefirmwarehub.com/catalog.json";
    static juce::StringArray userLists()
    {
        if (! userListsFile().existsAsFile()) return juce::StringArray (hubList);     // par defaut : la liste de The Firmware Hub
        auto a = juce::StringArray::fromLines (userListsFile().loadFileAsString()); a.trim(); a.removeEmptyStrings(); return a;
    }
    static void setUserLists (const juce::StringArray& a) { FirmwareInstaller::folder().createDirectory(); userListsFile().replaceWithText (a.joinIntoString ("\n")); }
    static juce::File userListCache (const juce::String& url)
    {
        return FirmwareInstaller::folder().getChildFile ("Lists").getChildFile (juce::String::toHexString (url.hashCode64()) + ".json");
    }
    static juce::String listLabel (const juce::String& url) { return juce::URL (url).getDomain().replace ("www.", ""); }
    // une liste au format de FM-9, ou celle de thefirmwarehub.com (nom, auteur, depot, "what", paquet relatif "pkg")
    static std::vector<CatalogEntry> parseAny (const juce::String& json, const juce::String& url)
    {
        auto fm9 = parse (json);
        const auto v = juce::JSON::parse (json);
        auto* arr = v.getProperty ("firmwares", {}).getArray();
        if (arr == nullptr || (! fm9.empty() && arr->size() > 0 && (*arr)[0].hasProperty ("compatible"))) return fm9;
        std::vector<CatalogEntry> out;
        const auto base = url.upToLastOccurrenceOf ("/", true, false);
        for (auto& f : *arr)
        {
            CatalogEntry e;
            e.name = f.getProperty ("name", {}).toString();
            auto pkg = f.getProperty ("pkg", {}).toString();
            if (e.name.isEmpty() || pkg.isEmpty()) continue;
            e.author = f.getProperty ("author", {}).toString(); e.repo = f.getProperty ("repo", {}).toString();
            e.desc = f.getProperty ("what", f.getProperty ("desc", {})).toString();
            e.page = f.getProperty ("site", f.getProperty ("page", {})).toString();
            e.pkg = pkg.startsWith ("http") ? pkg : base + pkg;
            e.pkgVersion = f.getProperty ("version", {}).toString(); e.license = f.getProperty ("lic", {}).toString();
            e.added = f.getProperty ("added", {}).toString();
            e.group = listLabel (url); e.compatible = true;
            out.push_back (e);
        }
        return out;
    }
    // ajoute les firmwares absents (meme nom ou meme depot avec un seul firmware = doublon) ; complete la date d'ajout
    static void merge (std::vector<CatalogEntry>& list, const std::vector<CatalogEntry>& extra)
    {
        auto norm = [] (const juce::String& n) { return n.toLowerCase().retainCharacters ("abcdefghijklmnopqrstuvwxyz0123456789"); };
        std::map<juce::String, int> repoCount;
        for (auto& e : list) if (e.repo.isNotEmpty()) ++repoCount[e.repo.toLowerCase()];
        for (auto& x : extra)
        {
            bool dup = false;
            // meme firmware : meme nom, meme paquet, ou meme depot qui n'en porte qu'un (sauf variante nommee entre parentheses)
            const bool sameRepoOk = x.repo.isNotEmpty() && ! x.name.containsChar ('(') && repoCount[x.repo.toLowerCase()] == 1;
            for (auto& e : list)
                if (norm (e.name) == norm (x.name) || (x.pkg.isNotEmpty() && e.pkg == x.pkg)
                    || (sameRepoOk && e.repo.equalsIgnoreCase (x.repo)))
                {
                    dup = true;
                    if (e.added.isEmpty()) e.added = x.added;
                    break;
                }
            if (! dup) list.push_back (x);
        }
    }

    static bool download (const juce::String& url, juce::MemoryBlock& out, juce::String& error, std::function<void (float)> progress = {},
                          const std::atomic<bool>* cancel = nullptr)
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
            if (cancel != nullptr && cancel->load()) { error = "cancelled"; return false; }
            const int n = in->read (chunk, 65536);
            if (n <= 0) break;
            out.append (chunk, (size_t) n);
            if (progress && total > 0) progress (juce::jlimit (0.0f, 1.0f, (float) out.getSize() / (float) total));
        }
        return out.getSize() > 0;
    }

    // installe le paquet materiel (.fwsc) et la version web (.wasm) quand ils existent ; thread d'arriere-plan
    // une page web peut contenir la version web en base64 (ex. la page "Try it in your browser" de Golana)
    static bool extractWasm (juce::MemoryBlock& data)
    {
        auto* p = static_cast<const char*> (data.getData());
        if (data.getSize() >= 4 && p[0] == 0 && p[1] == 'a' && p[2] == 's' && p[3] == 'm') return true;
        // recherche directe dans les octets (rapide meme sur une page de plusieurs centaines de Ko)
        const size_t n = data.getSize();
        size_t at = 0;
        for (; at + 6 <= n; ++at) if (std::memcmp (p + at, "AGFzbQ", 6) == 0) break;
        if (at + 6 > n) return false;
        size_t end = at;
        while (end < n && (std::isalnum ((unsigned char) p[end]) || p[end] == '+' || p[end] == '/' || p[end] == '=')) ++end;
        juce::MemoryOutputStream out;
        if (! juce::Base64::convertFromBase64 (out, juce::String (p + at, end - at))) return false;
        data = out.getMemoryBlock();
        return data.getSize() > 8;
    }

    // demarrage d'essai d'une version web, borne dans le temps : une version web qui ne rend pas la main
    // (boucle dans son init) n'est pas gardee, au lieu de bloquer l'installation et la fenetre
    static bool webBuildStarts (const juce::MemoryBlock& wasm, juce::String& err, const std::atomic<bool>* cancel, int timeoutMs = 20000)
    {
        struct Trial { juce::MemoryBlock data; juce::String msg; std::atomic<int> state { 0 }; };   // 0 en cours, 1 ok, 2 echec
        auto t = std::make_shared<Trial>(); t->data = wasm;
        std::thread ([t]
        {
            juce::String e;
            auto h = FirmwareHost::create (t->data.getData(), t->data.getSize(), juce::File(), e);
            t->msg = e;
            t->state.store (h != nullptr ? 1 : 2);
        }).detach();
        for (int waited = 0; t->state.load() == 0; waited += 50)
        {
            if (waited >= timeoutMs) { err = "did not start (skipped)"; return false; }
            if (cancel != nullptr && cancel->load()) { err = "cancelled"; return false; }
            juce::Thread::sleep (50);
        }
        if (t->state.load() == 2) { err = t->msg; return false; }
        return true;
    }

    static bool install (const CatalogEntry& e, juce::String& error, std::function<void (float)> progress = {}, const std::atomic<bool>* cancel = nullptr)
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
            if (! download (e.pkg, fw, err, part (0.0f), cancel)) errs << "package: " << err << "  ";
            else if (! FirmwareHost::isHardwarePackage (fw.getData(), fw.getSize())) errs << "package: not an FM-1 update package  ";
            else if (FirmwareInstaller::folder().getChildFile (e.fileName() + ".fwsc").replaceWithData (fw.getData(), fw.getSize())) { ++done; note ("package", e.pkg); }
        }
        if (e.web())
        {
            juce::MemoryBlock wasm; juce::String err;
            if (! download (e.url, wasm, err, part (e.hardware() ? 1.0f : 0.0f), cancel)) errs << "web build: " << err;
            else if (! extractWasm (wasm)) errs << "web build: no web build found on the page";
            else if (FirmwareInstaller::abiOf (wasm.getData(), wasm.getSize()) == 0) errs << "web build: not an FM-1 web build";
            else if (! webBuildStarts (wasm, err, cancel)) errs << "web build: " << err;
            else if (FirmwareInstaller::folder().getChildFile (e.fileName() + ".wasm").replaceWithData (wasm.getData(), wasm.getSize())) { ++done; if (! e.hardware()) note ("file", e.url); }
        }
        if (done == 0) error = errs.trim();
        return done > 0;
    }
};

// ------------------------------------------------------------------------------- thefirmwarehub.com
// installation depuis un lien du site : page d'un firmware (/firmware/<id>/), lien de jeu (?fw=<id>), paquet (/fw/<nom>.fwsc),
// ou simplement le nom du firmware. Le site publie la liste complete dans catalog.json (adresse du paquet, version, auteur).
struct FirmwareHub
{
    static constexpr const char* base = "https://thefirmwarehub.com/";
    static bool isHubLink (const juce::String& t) { return t.containsIgnoreCase ("thefirmwarehub.com"); }
    static juce::String keyOf (const juce::String& link)
    {
        auto t = link.trim();
        if (t.containsIgnoreCase ("/firmware/")) return t.fromFirstOccurrenceOf ("/firmware/", false, true).upToFirstOccurrenceOf ("/", false, false).upToFirstOccurrenceOf ("?", false, false);
        if (t.containsIgnoreCase ("fw=")) return t.fromFirstOccurrenceOf ("fw=", false, true).upToFirstOccurrenceOf ("&", false, false);
        if (t.endsWithIgnoreCase (".fwsc")) return t.fromLastOccurrenceOf ("/", false, false);
        if (isHubLink (t)) return {};
        return t;
    }
    // installe et rend le nom a lancer (paquet materiel), vide si echec
    static juce::String install (const juce::String& link, juce::String& error, std::function<void (float)> progress = {})
    {
        const auto key = keyOf (link);
        if (key.isEmpty()) { error = "Paste the link of a firmware page (thefirmwarehub.com/firmware/...) or its name."; return {}; }
        juce::MemoryBlock data; juce::String err;
        if (! FirmwareCatalog::download (juce::String (base) + "catalog.json", data, err)) { error = "The Firmware Hub: " + err; return {}; }
        const auto v = juce::JSON::parse (data.toString());
        auto* arr = v.getProperty ("firmwares", {}).getArray();
        if (arr == nullptr) { error = "The Firmware Hub: unreadable list"; return {}; }
        for (auto& f : *arr)
        {
            const auto id = f.getProperty ("id", {}).toString(), name = f.getProperty ("name", {}).toString(), pkg = f.getProperty ("pkg", {}).toString();
            const bool match = key.equalsIgnoreCase (id) || key.equalsIgnoreCase (name) || (key.endsWithIgnoreCase (".fwsc") && pkg.endsWithIgnoreCase (key))
                               || (key.endsWithIgnoreCase (".fwsc") && key.upToFirstOccurrenceOf ("-", false, false).equalsIgnoreCase (id));
            if (! match) continue;
            if (pkg.isEmpty()) { error = name + " has no FM-1 package on The Firmware Hub."; return {}; }
            CatalogEntry e;
            e.name = name; e.author = f.getProperty ("author", {}).toString(); e.repo = f.getProperty ("repo", {}).toString();
            e.license = f.getProperty ("lic", {}).toString(); e.pkg = juce::String (base) + pkg; e.compatible = true;
            e.pkgVersion = f.getProperty ("version", {}).toString();
            if (! FirmwareCatalog::install (e, error, progress)) return {};
            return e.fileName() + FirmwareInstaller::hardwareSuffix;
        }
        // lien de jeu (?fw=zp12n) : l'identifiant du firmware le plus long par lequel la cle commence
        if (! key.endsWithIgnoreCase (".fwsc"))
        {
            juce::String best;
            for (auto& f : *arr) { const auto id = f.getProperty ("id", {}).toString(); if (id.length() > best.length() && key.startsWithIgnoreCase (id)) best = id; }
            if (best.isNotEmpty() && best != key) return install (best, error, progress);
        }
        error = "\"" + key + "\" was not found on The Firmware Hub.";
        return {};
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
    std::function<void (const juce::String&)> onDelete;          // suppression complete d'un firmware installe (nom du fichier)
    void refreshInstalled() { installed = FirmwareInstaller::installed(); repaint(); list.repaint(); }
    void reloadEntries()
    {
        entries = FirmwareCatalog::load (date); installed = FirmwareInstaller::installed();
        { const juce::ScopedLock sl (previewLock); previews.clear(); previewFailed.clear(); }
        layoutList(); repaint();
    }
    std::function<void()> onClose;
    std::function<void()> onInstallFile, onInstallLink;          // installation manuelle (fichier .fwsc / .wasm, lien GitHub ou Firmware Hub)
    NineHandsMenu::MenuLnf menuLnf;                              // menus au style de FM-9

    FirmwareCatalogView (juce::Typeface::Ptr reg, juce::Typeface::Ptr med, juce::Typeface::Ptr bold) : fReg (reg), fMed (med), fBold (bold)
    {
        entries = FirmwareCatalog::load (date);
        menuLnf.reg = reg; menuLnf.med = med; menuLnf.bold = bold; menuLnf.textScale = 0.82f;
        installed = FirmwareInstaller::installed();
        loadFavorites();
        addAndMakeVisible (viewport);
        viewport.setViewedComponent (&list, false);
        viewport.setScrollBarsShown (true, false);
        viewport.setScrollBarThickness (8);
        viewport.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, NineHandsMenu::kFaint);
        list.owner = this;
        list.setMouseCursor (juce::MouseCursor::NormalCursor);
        // recherche (nom, auteur, description) et filtre Compatible only
        using namespace NineHandsMenu;
        search.setFont (font (fReg, 13.0f));
        search.setTextToShowWhenEmpty ("Search firmwares", kFaint);
        search.setIndents (38, 0);
        search.setJustification (juce::Justification::centredLeft);
        search.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);   // fond arrondi dessine par la fenetre
        search.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        search.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
        search.setColour (juce::TextEditor::textColourId, kText);
        search.setColour (juce::TextEditor::highlightColourId, kAccent.withAlpha (0.35f));
        search.setColour (juce::CaretComponent::caretColourId, kAccent);
        search.onTextChange = [this] { layoutList(); viewport.setViewPosition (0, 0); repaint(); };
        search.onEscapeKey = [this] { search.clear(); layoutList(); repaint(); };
        search.onFocusLost = [this] { repaint(); };
        search.onReturnKey = [this] { repaint(); };
        addAndMakeVisible (search);
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
        search.setBounds (searchRect().reduced (2));
        r.removeFromTop (headerH);
        r.removeFromBottom (footerH);
        viewport.setBounds (r);
        layoutList();
    }

    void paint (juce::Graphics& g) override
    {
        using namespace NineHandsMenu;
        if (! floating) g.fillAll (juce::Colours::black);   // fond noir opaque : la facade de FM-9 ne se voit plus
        else
        {
            // fenetre flottante : ombre douce autour de la boite arrondie, le reste transparent
            for (int k = 6; k >= 1; --k)
            {
                g.setColour (juce::Colours::black.withAlpha (0.07f));
                g.fillRoundedRectangle (getLocalBounds().reduced (8 - k).toFloat().translated (0.0f, 2.0f), 12.0f + (float) k);
            }
        }
        auto box = getLocalBounds().reduced (8).toFloat();
        g.setColour (kBg); g.fillRoundedRectangle (box, 12.0f);
        g.setColour (kBorder); g.drawRoundedRectangle (box, 12.0f, 1.0f);
        auto r = getLocalBounds().reduced (18);
        auto head = r.removeFromTop (headerH);
        g.setColour (kText); g.setFont (font (fBold, 20.0f));
        g.drawText ("FM-1 Firmware Catalog", head.removeFromTop (40), juce::Justification::centredLeft);
        int ok = 0; for (auto& e : entries) ok += e.compatible ? 1 : 0;
        g.setColour (kDim); g.setFont (font (fReg, 13.0f));
        const bool filtered = filterMode != 0 || search.getText().trim().isNotEmpty();
        g.drawText (juce::String (ok) + " compatible / " + juce::String ((int) entries.size()) + " firmwares"
                    + (filtered ? "  -  " + juce::String (shownCount) + " shown" : juce::String())
                    + (date.isNotEmpty() ? "  -  " + date : juce::String()), head.removeFromTop (28), juce::Justification::centredLeft);
        {
            // champ de recherche : pastille arrondie et loupe (le texte est saisi par-dessus)
            auto sr = searchRect().toFloat();
            const bool foc = search.hasKeyboardFocus (true);
            g.setColour (kHover); g.fillRoundedRectangle (sr, 7.0f);
            g.setColour (foc ? kAccent : kBorder); g.drawRoundedRectangle (sr.reduced (0.5f), 7.0f, 1.0f);
            const float cx = sr.getX() + 21.0f, cy = sr.getCentreY() - 1.5f, rr = 6.5f;
            g.setColour (foc ? kAccent : kDim);
            g.drawEllipse (cx - rr, cy - rr, 2 * rr, 2 * rr, 1.8f);
            g.drawLine (cx + rr * 0.72f, cy + rr * 0.72f, cx + rr * 1.55f, cy + rr * 1.55f, 2.2f);
            // filtre : selecteur segmente All / Compatible / Installed (compteurs)
            const auto segs = segmentRects();
            const auto fr = filterRect().toFloat();
            g.setColour (kHover); g.fillRoundedRectangle (fr, 6.0f);
            g.setColour (kBorder); g.drawRoundedRectangle (fr.reduced (0.5f), 6.0f, 1.0f);
            for (int i = 0; i < numSegs; ++i)
            {
                const auto sr2 = segs[(size_t) i].toFloat();
                if (i == filterMode) { g.setColour (kAccent); g.fillRoundedRectangle (sr2.reduced (3.0f), 4.0f); }
                g.setColour (i == filterMode ? juce::Colour (0xff0b1520) : kText); g.setFont (font (fMed, 13.0f));
                g.drawText (segmentLabel (i), sr2, juce::Justification::centred);
            }
        }
        // pied : etat + boutons
        auto foot = getLocalBounds().reduced (18).removeFromBottom (footerH).toFloat();
        g.setColour (status.startsWith ("!") ? juce::Colour (0xffff8a80) : kDim); g.setFont (font (fReg, 13.0f));
        g.drawText (status.trimCharactersAtStart ("!"), foot.withTrimmedRight (550).toNearestInt(), juce::Justification::centredLeft);
        drawButton (g, btnClose (), "Close", false);
        drawButton (g, btnRefresh (), busy ? "..." : "Refresh", false);
        drawButton (g, btnAll (), "Install all compatible", true);
        drawButton (g, btnAdd (), "Add...", false);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto p = e.getPosition();
        if (btnClose().contains (p))
        {
            // installation en cours : on annule, la fenetre se ferme des que le travail s'arrete (jamais de blocage)
            if (busy && worker.joinable() && ! finished.load()) { stop = true; closeWhenDone = true; status = "Cancelling..."; repaint(); return; }
            if (onClose) onClose();
            return;
        }
        {
            const auto segs = segmentRects();
            for (int i = 0; i < numSegs; ++i)
                if (segs[(size_t) i].contains (p)) { filterMode = i; layoutList(); viewport.setViewPosition (0, 0); repaint(); return; }
        }
        if (busy) return;
        if (btnAdd().contains (p)) { showAddMenu(); return; }
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
    // en-tete, a droite du titre : recherche puis filtre
    juce::String segmentLabel (int i) const
    {
        int nc = 0, ni = 0;
        for (auto& e : entries) { nc += e.compatible ? 1 : 0; ni += anyInstalled (e) ? 1 : 0; }
        if (i == 3) return "Favorites  " + juce::String (favorites.size());
        if (i == 4) { int nn = 0; for (auto& e : entries) nn += isNew (e) ? 1 : 0; return "New  " + juce::String (nn); }
        return i == 0 ? juce::String ("All") : i == 1 ? "Compatible  " + juce::String (nc) : "Installed  " + juce::String (ni);
    }
    static constexpr int numSegs = 5;
    // New : ajoute sur thefirmwarehub.com ces 7 derniers jours
    bool isNew (const CatalogEntry& e) const { return e.added.isNotEmpty() && e.added >= newSince; }
    juce::String newSince = (juce::Time::getCurrentTime() - juce::RelativeTime::days (7)).formatted ("%Y-%m-%d");
    // favoris : fichier Favorites.txt du dossier des firmwares (un nom par ligne)
    juce::StringArray favorites;
    static juce::File favoritesFile() { return FirmwareInstaller::folder().getChildFile ("Favorites.txt"); }
    void loadFavorites() { favorites.clear(); favorites.addLines (favoritesFile().loadFileAsString()); favorites.removeEmptyStrings(); }
    void toggleFavorite (const CatalogEntry& e)
    {
        if (favorites.contains (e.name)) favorites.removeString (e.name); else favorites.add (e.name);
        FirmwareInstaller::folder().createDirectory();
        favoritesFile().replaceWithText (favorites.joinIntoString ("\n"));
        if (filterMode == 3) layoutList();
        repaint(); list.repaint();
    }
    std::array<juce::Rectangle<int>, numSegs> segmentRects() const
    {
        // largeur de chaque segment : son texte + une marge
        std::array<juce::Rectangle<int>, numSegs> r;
        auto a = filterRect();
        for (int i = 0; i < numSegs; ++i)
        {
            const int w = juce::roundToInt (juce::GlyphArrangement::getStringWidth (font (fMed, 13.0f), segmentLabel (i))) + 24;
            r[(size_t) i] = a.removeFromLeft (w);
        }
        return r;
    }
    juce::Rectangle<int> filterRect() const
    {
        int w = 6;
        for (int i = 0; i < numSegs; ++i) w += juce::roundToInt (juce::GlyphArrangement::getStringWidth (font (fMed, 13.0f), segmentLabel (i))) + 24;
        return getLocalBounds().reduced (18).removeFromTop (40).removeFromRight (w).withSizeKeepingCentre (w, 34);
    }
    juce::Rectangle<int> searchRect() const
    {
        auto a = getLocalBounds().reduced (18).removeFromTop (40);
        a.removeFromRight (filterRect().getWidth() + 12);
        const int title = juce::roundToInt (juce::GlyphArrangement::getStringWidth (font (fBold, 20.0f), "FM-1 Firmware Catalog")) + 18;
        a.removeFromLeft (title);
        return a.withSizeKeepingCentre (juce::jmin (a.getWidth(), 340), 34).withX (a.getRight() - juce::jmin (a.getWidth(), 340));
    }
    // boutons du pied, plus compacts (hauteur 36, comme la recherche)
    juce::Rectangle<int> footArea() const { auto a = getLocalBounds().reduced (18).removeFromBottom (footerH).withTrimmedTop (8); return a.withSizeKeepingCentre (a.getWidth(), 34); }   // boutons compacts, texte inchange
    juce::Rectangle<int> btnClose() const   { return footArea().removeFromRight (90); }
    juce::Rectangle<int> btnRefresh() const { auto a = footArea(); a.removeFromRight (100); return a.removeFromRight (104); }
    juce::Rectangle<int> btnAdd() const     { auto a = footArea(); a.removeFromRight (212 + 176 + 12); return a.removeFromRight (96); }
    juce::String listLabels() const
    {
        juce::StringArray l;
        for (auto& u : FirmwareCatalog::userLists()) l.addIfNotAlreadyThere (FirmwareCatalog::listLabel (u));
        return l.isEmpty() ? juce::String() : ", " + l.joinIntoString (", ");
    }
    void showAddMenu()
    {
        juce::PopupMenu m, rem;
        m.setLookAndFeel (&menuLnf);
        m.addSectionHeader ("Install manually");
        m.addItem (1, "Install a firmware file (.fwsc or .wasm)...", onInstallFile != nullptr);
        m.addItem (2, "Install from a link (GitHub or The Firmware Hub)...", onInstallLink != nullptr);
        m.addSeparator();
        m.addSectionHeader ("Firmware lists");
        m.addItem (3, "Add a firmware list (link to its JSON)...");
        const auto lists = FirmwareCatalog::userLists();
        for (int i = 0; i < lists.size(); ++i) rem.addItem (10 + i, lists[i]);
        m.addSubMenu ("Remove a list", rem, ! lists.isEmpty());
        m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [self = juce::Component::SafePointer<FirmwareCatalogView> (this), lists] (int r)
        {
            if (self == nullptr || r == 0) return;
            if (r == 1 && self->onInstallFile) self->onInstallFile();
            else if (r == 2 && self->onInstallLink) self->onInstallLink();
            else if (r == 3) self->askNewList();
            else if (r >= 10 && r - 10 < lists.size())
            {
                auto l = lists; FirmwareCatalog::userListCache (l[r - 10]).deleteFile(); l.remove (r - 10);
                FirmwareCatalog::setUserLists (l);
                self->entries = FirmwareCatalog::load (self->date); self->layoutList(); self->status = "List removed."; self->repaint();
            }
        });
    }
    void askNewList()
    {
        auto* w = new juce::AlertWindow ("Add a firmware list",
                                         "Link to a list of FM-1 firmwares (JSON): an FM-9 catalog or a Firmware Hub style catalog.json.\n"
                                         "Its firmwares are added to the catalog without duplicates and updated with Refresh.",
                                         juce::MessageBoxIconType::NoIcon, this);
        w->addTextEditor ("url", "https://");
        w->addButton ("Add", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([self = juce::Component::SafePointer<FirmwareCatalogView> (this), w] (int r)
        {
            if (self == nullptr || r != 1) return;
            const auto url = w->getTextEditorContents ("url").trim();
            if (! url.startsWith ("http")) return;
            auto l = FirmwareCatalog::userLists(); l.addIfNotAlreadyThere (url);
            FirmwareCatalog::setUserLists (l);
            self->refresh();                                   // telecharge la nouvelle liste et la fusionne
        }), true);
    }
    juce::Rectangle<int> btnAll() const     { auto a = footArea(); a.removeFromRight (212); return a.removeFromRight (176); }

    void drawButton (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& t, bool primary) const
    {
        using namespace NineHandsMenu;
        const bool dim = busy && t != "Close";
        g.setColour (primary ? kAccent.withAlpha (dim ? 0.25f : 0.9f) : kHover); g.fillRoundedRectangle (r.toFloat(), 7.0f);
        if (! primary) { g.setColour (kBorder); g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 7.0f, 1.0f); }
        g.setColour (primary ? juce::Colour (0xff0b1520) : (dim ? kFaint : kText)); g.setFont (font (fMed, 13.5f));
        g.drawText (t, r, juce::Justification::centred);
    }

    // ---------------------------------------------------------------- galerie : une fiche par firmware
    // ecran du firmware, nom, description, puis trois boutons : telecharger (fleche), charger (coche verte), supprimer (corbeille)
    struct Item { int entry = -1; juce::String group; juce::Rectangle<int> r; };
    std::vector<Item> items;
    static constexpr int gap = 10, minCard = 128, cardPad = 7;

    void layoutList()
    {
        items.clear();
        // compatibles d'abord dans chaque famille
        std::vector<int> idx ((size_t) entries.size());
        for (size_t i = 0; i < idx.size(); ++i) idx[i] = (int) i;
        std::stable_sort (idx.begin(), idx.end(), [this] (int a, int b) { return entries[(size_t) a].compatible && ! entries[(size_t) b].compatible; });
        juce::StringArray groups;
        for (auto& e : entries) groups.addIfNotAlreadyThere (e.group);
        const auto q = search.getText().trim().toLowerCase();
        auto keep = [this, &q] (const CatalogEntry& e)
        {
            if (filterMode == 1 && ! e.compatible) return false;
            if (filterMode == 2 && ! anyInstalled (e)) return false;
            if (filterMode == 3 && ! favorites.contains (e.name)) return false;
            if (filterMode == 4 && ! isNew (e)) return false;
            if (q.isEmpty()) return true;
            for (auto& w : juce::StringArray::fromTokens (q, " ", ""))       // chaque mot doit apparaitre quelque part
                if (! (e.name + " " + e.author + " " + e.desc + " " + e.group).toLowerCase().contains (w)) return false;
            return true;
        };
        const int W = juce::jmax (100, viewport.getWidth() - 12);
        const int cols = juce::jmax (2, (W + gap) / (minCard + gap));
        const int cw = (W - gap * (cols - 1)) / cols;
        const int ch = cardH (cw);
        // une seule grille (les familles se suivent : M-VAVE, Felucca...), compatibles d'abord
        std::vector<int> found;
        for (auto& grp : groups) for (int i : idx) if (entries[(size_t) i].group == grp && entries[(size_t) i].compatible && keep (entries[(size_t) i])) found.push_back (i);
        for (auto& grp : groups) for (int i : idx) if (entries[(size_t) i].group == grp && ! entries[(size_t) i].compatible && keep (entries[(size_t) i])) found.push_back (i);
        if (filterMode == 4)      // New : les plus recents d'abord
            std::stable_sort (found.begin(), found.end(), [this] (int a, int b) { return entries[(size_t) a].added > entries[(size_t) b].added; });
        shownCount = (int) found.size();
        int y = 0;
        for (size_t k = 0; k < found.size(); ++k)
        {
            const int c = (int) (k % (size_t) cols);
            if (c == 0 && k > 0) y += ch + gap;
            items.push_back ({ found[k], {}, { c * (cw + gap), y, cw, ch } });
        }
        if (! found.empty()) y += ch + gap;
        list.setSize (W, juce::jmax (10, y));
        list.repaint();
    }
    static int cardH (int cw) { return cw - 2 * cardPad + cardPad + 6 + 22 + 34 + 38; }   // ecran carre + nom + 2 lignes + boutons

    // zones d'une fiche
    static juce::Rectangle<int> screenRect (juce::Rectangle<int> c) { const int s = c.getWidth() - 2 * cardPad; return { c.getX() + cardPad, c.getY() + cardPad, s, s }; }
    static juce::Point<float> btnCentre (juce::Rectangle<int> c, int k) { return { c.getX() + cardPad + 13.0f + k * 32.0f, c.getBottom() - 21.0f }; }
    // etoile des favoris : en haut a droite de l'ecran du firmware
    static juce::Rectangle<float> starRect (juce::Rectangle<int> c) { const auto sr = screenRect (c).toFloat(); return { sr.getX() + 5.0f, sr.getY() + 5.0f, 16.0f, 16.0f }; }
    static void drawStar (juce::Graphics& g, juce::Rectangle<float> r, bool on, bool hot)
    {
        g.setColour (juce::Colours::black.withAlpha (on || hot ? 0.55f : 0.40f)); g.fillEllipse (r);
        juce::Path st; st.addStar (r.getCentre(), 5, r.getWidth() * 0.19f, r.getWidth() * 0.40f, 0.0f);
        const juce::Colour gold (0xffffc83d);
        if (on) { g.setColour (gold); g.fillPath (st); }
        else { g.setColour (hot ? gold : juce::Colours::white.withAlpha (0.85f)); g.strokePath (st, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); }
    }
    // bouton Load : pastille a droite des trois icones
    static juce::Rectangle<float> loadRect (juce::Rectangle<int> c)
    {
        const auto b2 = btnCentre (c, 2);
        const float x0 = b2.x + 18.0f, x1 = (float) c.getRight() - cardPad - 2.0f;
        return { x0, b2.y - 12.0f, juce::jmax (30.0f, x1 - x0), 24.0f };
    }
    static int buttonAt (juce::Rectangle<int> c, juce::Point<int> p)
    {
        for (int k = 0; k < 3; ++k) if (btnCentre (c, k).getDistanceFrom (p.toFloat()) <= 14.0f) return k;
        if (loadRect (c).contains (p.toFloat())) return 3;
        if (starRect (c).contains (p.toFloat())) return 4;
        return -1;
    }
    bool anyInstalled (const CatalogEntry& e) const { return installed.contains (e.fileName()) || installed.contains (e.fileName() + FirmwareInstaller::hardwareSuffix); }


    static void drawArrow (juce::Graphics& g, juce::Point<float> c, float r, juce::Colour col, bool filled)
    {
        if (filled) { g.setColour (col); g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r); }
        g.setColour (filled ? NineHandsMenu::kBg : col);
        const float w = juce::jmax (1.6f, r * 0.17f);
        g.drawLine (c.x, c.y - r * 0.48f, c.x, c.y + r * 0.36f, w);
        juce::Path p; p.startNewSubPath (c.x - r * 0.36f, c.y + r * 0.02f); p.lineTo (c.x, c.y + r * 0.40f); p.lineTo (c.x + r * 0.36f, c.y + r * 0.02f);
        g.strokePath (p, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    static void drawCheck (juce::Graphics& g, juce::Point<float> c, float r, bool on)
    {
        using namespace NineHandsMenu;
        const float w = juce::jmax (1.6f, r * 0.17f);
        if (on) { g.setColour (juce::Colour (0xff4ccf78)); g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r); g.setColour (kBg); }
        else { g.setColour (kFaint); g.drawEllipse (c.x - r + 0.75f, c.y - r + 0.75f, 2 * r - 1.5f, 2 * r - 1.5f, 1.4f); }
        juce::Path p; p.startNewSubPath (c.x - r * 0.40f, c.y + r * 0.02f); p.lineTo (c.x - r * 0.10f, c.y + r * 0.32f); p.lineTo (c.x + r * 0.42f, c.y - r * 0.30f);
        g.strokePath (p, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    static void drawTrash (juce::Graphics& g, juce::Point<float> c, float r, juce::Colour col)
    {
        g.setColour (col);
        const float w = juce::jmax (1.4f, r * 0.14f);
        g.drawRoundedRectangle (c.x - r * 0.40f, c.y - r * 0.36f, r * 0.80f, r * 0.98f, r * 0.12f, w);
        g.drawLine (c.x - r * 0.58f, c.y - r * 0.48f, c.x + r * 0.58f, c.y - r * 0.48f, w);
        g.drawLine (c.x - r * 0.18f, c.y - r * 0.66f, c.x + r * 0.18f, c.y - r * 0.66f, w);
        for (float k : { -0.15f, 0.15f }) g.drawLine (c.x + r * k, c.y - r * 0.16f, c.x + r * k, c.y + r * 0.40f, juce::jmax (1.0f, w - 0.4f));
    }
    static void drawRing (juce::Graphics& g, juce::Point<float> c, float r, float f, juce::Colour col)
    {
        g.setColour (NineHandsMenu::kFaint); g.drawEllipse (c.x - r, c.y - r, 2 * r, 2 * r, r * 0.22f);
        juce::Path a; a.addCentredArc (c.x, c.y, r, r, 0.0f, 0.0f, juce::MathConstants<float>::twoPi * juce::jlimit (0.02f, 1.0f, f), true);
        g.setColour (col); g.strokePath (a, juce::PathStrokeType (r * 0.22f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    juce::String updateLabel (const CatalogEntry& e) const { return e.pkgVersion.isNotEmpty() && e.pkgVersion.length() <= 9 ? "UPDATE v" + e.pkgVersion : juce::String ("UPDATE"); }

    struct List : juce::Component
    {
        FirmwareCatalogView* owner = nullptr;
        int hotBtn = -1;
        void paint (juce::Graphics& g) override
        {
            using namespace NineHandsMenu;
            auto& o = *owner;
            const auto clip = g.getClipBounds();
            const juce::Colour orange (0xffffa726), red (0xffff5a52), blue = kAccent;
            for (auto& it : o.items)
            {
                if (! it.r.intersects (clip)) continue;
                if (it.entry < 0)
                {
                    g.setColour (kDim); g.setFont (font (o.fBold, 11.5f).withExtraKerningFactor (0.14f));
                    g.drawText (it.group.toUpperCase(), it.r.withTrimmedLeft (4).withTrimmedTop (10), juce::Justification::centredLeft);
                    continue;
                }
                const auto& e = o.entries[(size_t) it.entry];
                const auto c = it.r.toFloat();
                const bool inst = o.anyInstalled (e);
                const bool upd = e.compatible && e.needsUpdate (o.installed);
                const bool doing = o.current == it.entry, loading = o.loadingIdx == it.entry;
                // fiche
                g.setColour (it.entry == o.hover ? kHover.brighter (0.06f) : kHover); g.fillRoundedRectangle (c, 10.0f);
                g.setColour (upd || (doing && inst) ? orange : kBorder); g.drawRoundedRectangle (c.reduced (0.75f), 10.0f, upd ? 1.8f : 1.0f);
                // ecran du firmware
                const auto sr = screenRect (it.r).toFloat();
                juce::Image img;
                {
                    const juce::ScopedLock sl (o.previewLock);
                    if (auto f = o.previews.find (it.entry); f != o.previews.end()) img = f->second;
                }
                {
                    juce::Graphics::ScopedSaveState ss (g);
                    juce::Path clipP; clipP.addRoundedRectangle (sr, 7.0f); g.reduceClipRegion (clipP);
                    g.setColour (juce::Colour (0xff06080b)); g.fillRect (sr);
                    if (img.isValid())
                    {
                        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
                        g.setOpacity (e.compatible ? 1.0f : 0.35f);
                        g.drawImage (img, sr, juce::RectanglePlacement::stretchToFit);
                        g.setOpacity (1.0f);
                    }
                    else
                    {
                        // en attendant l'ecran : le nom en lettres d'afficheur ; pas d'ecran possible : ecran noir
                        bool failed;
                        { const juce::ScopedLock sl (o.previewLock); failed = o.previewFailed.count (it.entry) > 0; }
                        if (! failed || ! e.compatible)
                        {
                            g.setColour (kFaint); g.setFont (font (o.fMed, 9.0f));
                            g.drawFittedText (e.compatible ? e.name : "Not compatible", sr.reduced (12.0f).toNearestInt(), juce::Justification::centred, 3);
                        }
                    }
                }
                if (upd)
                {
                    // bandeau orange en haut a droite de l'ecran
                    const auto lab = o.updateLabel (e);
                    auto ft = font (o.fBold, 7.6f);
                    const float tw = juce::GlyphArrangement::getStringWidth (ft, lab) + 18.0f;
                    auto b = juce::Rectangle<float> (sr.getRight() - tw - 6.0f, sr.getY() + 6.0f, tw, 22.0f);    // en haut a droite
                    g.setColour (orange); g.fillRoundedRectangle (b, 11.0f);
                    g.setColour (kBg); g.setFont (ft); g.drawText (lab, b, juce::Justification::centred);
                }
                drawStar (g, starRect (it.r), o.favorites.contains (e.name), it.entry == o.hover && hotBtn == 4);
                if (o.isNew (e))
                {
                    // nouveaute sur la liste des firmwares : NEW en bas a droite de l'ecran
                    const auto nf = font (o.fBold, 8.0f);
                    const float nw = juce::GlyphArrangement::getStringWidth (nf, "NEW") + 6.0f;      // contour serre autour du texte
                    auto nb = juce::Rectangle<float> (sr.getRight() - nw - 2.0f, sr.getBottom() - 16.0f, nw, 14.0f);
                    g.setColour (juce::Colours::black.withAlpha (0.6f)); g.fillRoundedRectangle (nb, 3.0f);
                    g.setColour (juce::Colour (0xff5be08a)); g.setFont (nf);
                    g.drawText ("NEW", nb, juce::Justification::centred);
                }
                // nom et description
                auto tr = it.r.withTrimmedTop (juce::roundToInt (sr.getHeight()) + cardPad + 6).reduced (cardPad + 2, 0);
                g.setColour (e.compatible ? kText : kText.withAlpha (0.55f)); g.setFont (font (o.fMed, 11.2f));
                g.drawText (e.name, tr.removeFromTop (22), juce::Justification::centredLeft, true);
                g.setColour (e.compatible ? kDim : juce::Colour (0xffff8a80).withAlpha (0.8f)); g.setFont (font (o.fReg, 8.6f));
                g.drawFittedText (e.compatible ? (e.vendor ? "M-VAVE's own firmware, downloaded from M-VAVE." : e.desc) : e.reason,
                                  tr.removeFromTop (34), juce::Justification::topLeft, 2, 1.0f);
                if (! e.compatible) continue;
                // boutons
                const auto b0 = btnCentre (it.r, 0), b1 = btnCentre (it.r, 1), b2 = btnCentre (it.r, 2);
                const float r = 11.0f;
                const bool hot = it.entry == o.hover;
                if (doing) drawRing (g, b0, r * 0.82f, o.installProg.load(), upd || inst ? orange : blue);
                else if (! inst) drawArrow (g, b0, r, (hot && hotBtn == 0) ? blue.brighter (0.25f) : blue, true);
                else if (upd) drawArrow (g, b0, r, (hot && hotBtn == 0) ? orange.brighter (0.2f) : orange, true);
                else drawArrow (g, b0, r, kFaint, false);
                if (loading) drawRing (g, b1, r * 0.82f, o.loadProg.load(), juce::Colour (0xff4ccf78));
                else drawCheck (g, b1, (hot && hotBtn == 1 && inst) ? r + 1.0f : r, inst);
                drawTrash (g, b2, r * 0.95f, inst && o.onDelete != nullptr ? ((hot && hotBtn == 2) ? juce::Colour (0xffff7a73) : red) : kFaint);
                if (! doing && ! loading)
                {
                    // Load : lance le firmware installe
                    const auto lr = loadRect (it.r);
                    const bool on = inst && ! o.busy;
                    g.setColour (on ? ((hot && hotBtn == 3) ? kAccent.brighter (0.2f) : kAccent) : kHover.brighter (0.05f));
                    g.fillRoundedRectangle (lr, 12.0f);
                    if (! on) { g.setColour (kBorder); g.drawRoundedRectangle (lr.reduced (0.5f), 12.0f, 1.0f); }
                    g.setColour (on ? juce::Colour (0xff0b1520) : kFaint); g.setFont (font (o.fMed, 8.8f));
                    g.drawText ("Load", lr, juce::Justification::centred);
                }
                if (doing || loading)
                {
                    const float f = loading ? o.loadProg.load() : o.installProg.load();
                    g.setColour (loading ? juce::Colour (0xff4ccf78) : (upd || inst ? orange : blue)); g.setFont (font (o.fMed, 8.4f));
                    g.drawText ((loading ? "Starting " : (upd || inst ? "Updating " : "")) + juce::String (juce::roundToInt (f * 100.0f)) + " %",
                                juce::Rectangle<float> (b2.x + 18.0f, b2.y - 10.0f, c.getRight() - b2.x - 22.0f, 20.0f), juce::Justification::centredLeft, true);
                }
            }
        }
        const Item* itemAt (juce::Point<int> p) const
        {
            for (auto& it : owner->items) if (it.entry >= 0 && it.r.contains (p)) return &it;
            return nullptr;
        }
        void mouseMove (const juce::MouseEvent& e) override
        {
            const auto* it = itemAt (e.getPosition());
            const int i = it != nullptr ? it->entry : -1;
            const int b = it != nullptr ? buttonAt (it->r, e.getPosition()) : -1;
            if (i != owner->hover || b != hotBtn) { owner->hover = i; hotBtn = b; repaint(); }
            setMouseCursor (b >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        }
        void mouseExit (const juce::MouseEvent&) override { owner->hover = -1; hotBtn = -1; repaint(); }
        void mouseDown (const juce::MouseEvent& ev) override
        {
            const auto* it = itemAt (ev.getPosition());
            if (it == nullptr) return;
            const int i = it->entry;
            const auto& e = owner->entries[(size_t) i];
            const bool inst = owner->anyInstalled (e);
            if (ev.mods.isPopupMenu())
            {
                // clic droit sur un firmware installe : le supprimer completement
                if (! inst || owner->onDelete == nullptr) return;
                juce::PopupMenu m;
                m.addItem (1, "Delete " + e.name + " (everything installed)");
                m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(),
                    [o = juce::Component::SafePointer<FirmwareCatalogView> (owner), stem = e.fileName()] (int r) { if (r == 1 && o != nullptr && o->onDelete) o->onDelete (stem); });
                return;
            }
            int b = buttonAt (it->r, ev.getPosition());
            if (b == 4) { owner->toggleFavorite (e); return; }
            if (! e.compatible) b = -1;
            if (b < 0)
            {
                // double-clic sur la fiche : page du projet
                const auto link = e.page.isNotEmpty() ? e.page : (e.repo.isNotEmpty() ? "https://github.com/" + e.repo : juce::String());
                if (link.isNotEmpty() && ev.getNumberOfClicks() > 1) juce::URL (link).launchInDefaultBrowser();
                return;
            }
            if (b == 2) { if (inst && owner->onDelete != nullptr) owner->onDelete (e.fileName()); return; }
            if (owner->busy) return;
            if (b == 1 || b == 3) { if (inst) owner->startLoad (i, e.loadName (owner->installed)); return; }
            if (! inst || e.needsUpdate (owner->installed)) owner->runInstall ({ e });     // fleche : telecharger / mettre a jour
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
                if (! FirmwareCatalog::install (e, err, [this] (float f) { installProg.store (f); }, &stop)) { const juce::ScopedLock sl (lock); failures.add (e.name + ": " + err); }
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
            // listes ajoutees : copie locale de chacune
            int listsOk = 0;
            for (auto& url : FirmwareCatalog::userLists())
            {
                if (stop) break;
                juce::MemoryBlock mb; juce::String e2;
                if (FirmwareCatalog::download (url, mb, e2, {}, &stop) && ! FirmwareCatalog::parseAny (mb.toString(), url).empty())
                {
                    auto f = FirmwareCatalog::userListCache (url);
                    f.getParentDirectory().createDirectory();
                    f.replaceWithText (mb.toString());
                    ++listsOk;
                }
            }
            const juce::ScopedLock sl (lock);
            if (st < 400 && ! FirmwareCatalog::parse (txt).empty())
            {
                FirmwareInstaller::folder().createDirectory();
                FirmwareCatalog::cacheFile().replaceWithText (txt);
                refreshed = 1;
            }
            else refreshed = listsOk > 0 ? 1 : -1;
            finished = true;
        });
        repaint();
    }

    // ---------------------------------------------------------------- fenetre agrandie (comme TONE3000 dans Ampire)
    // le catalogue devient une fenetre flottante plus grande que celle du VST, centree sur elle et limitee a l'ecran
public:
    static constexpr int bigW = 1360, bigH = 840;
    bool isFloating() const { return floating; }
    void floatOver (juce::Component& home)
    {
        if (floating) return;
        homeComp = &home;
        floating = true;
        setOpaque (false);
        addToDesktop (juce::ComponentPeer::windowIsTemporary | juce::ComponentPeer::windowIsSemiTransparent);
        placeOverHome();
        setVisible (true);
        toFront (true);
    }
    void placeOverHome()
    {
        auto* h = homeComp.getComponent();
        if (h == nullptr) return;
        const auto hb = h->getScreenBounds();
        homeBounds = hb;
        auto area = juce::Desktop::getInstance().getDisplays().getDisplayForRect (hb) != nullptr
                      ? juce::Desktop::getInstance().getDisplays().getDisplayForRect (hb)->userArea : hb;
        const int w = juce::jmin (juce::jmax (hb.getWidth(), bigW), area.getWidth() - 16);
        const int hgt = juce::jmin (juce::jmax (hb.getHeight(), bigH), area.getHeight() - 16);
        setBounds (juce::Rectangle<int> (w, hgt).withCentre (hb.getCentre()).constrainedWithin (area.reduced (8)));
    }
private:
    juce::Component::SafePointer<juce::Component> homeComp;
    juce::Rectangle<int> homeBounds;
    bool floating = false;
public:
    void timerCallback() override
    {
        if (floating)
        {
            // FM-9 deplace ou redimensionne : la fenetre suit ; FM-9 ferme ou cache : le catalogue se ferme
            auto* h = homeComp.getComponent();
            if (h == nullptr || ! h->isShowing()) { if (onClose) onClose(); return; }
            if (h->getScreenBounds() != homeBounds) placeOverHome();
        }
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
            if (closeWhenDone) { closeWhenDone = false; stop = false; installed = FirmwareInstaller::installed(); if (onClose) onClose(); return; }
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
            else if (failures.isEmpty()) status = juce::String (total) + (total > 1 ? " firmwares installed." : " firmware installed.") + " Press Load on its card to start it.";
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

    // capture : recherche et filtre
    void setFilterForTest (const juce::String& text, int compat)
    {
        filterMode = compat; search.setText (text, false); layoutList();
        // capture : ecrans deja en cache charges tout de suite
        for (auto& it : items)
            if (it.entry >= 0)
                if (auto img = juce::ImageFileFormat::loadFrom (previewFile (entries[(size_t) it.entry])); img.isValid()) previews[it.entry] = img;
                else if (anyInstalled (entries[(size_t) it.entry])) previewFailed.insert (it.entry);      // installe sans ecran possible : noir
        repaint();
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
        else if (e.web()) { FirmwareCatalog::download (e.url, data, err, stage (0)); FirmwareCatalog::extractWasm (data); }   // la version web est plus legere a telecharger
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
    // ecrans de la galerie : un a la fois, pour les fiches visibles (cache, puis image publiee, puis firmware installe)
    void tickPreview()
    {
        if (previewBusy.load() || ! isShowing()) return;
        const auto view = viewport.getViewArea().expanded (0, 240);
        int idx = -1;
        {
            const juce::ScopedLock sl (previewLock);
            for (auto& it : items)
                if (it.entry >= 0 && it.r.intersects (view) && entries[(size_t) it.entry].compatible
                    && ! previews.count (it.entry) && ! previewFailed.count (it.entry)) { idx = it.entry; break; }
        }
        if (idx < 0) return;
        if (previewThread.joinable()) previewThread.join();
        previewBusy.store (true);
        const juce::Component::SafePointer<FirmwareCatalogView> self (this);
        const auto e = entries[(size_t) idx];
        const bool local = anyInstalled (e);
        previewThread = std::thread ([this, self, idx, e, local]
        {
            auto img = galleryPreview (e, local);
            {
                const juce::ScopedLock sl (previewLock);
                if (img.isValid()) previews[idx] = img; else previewFailed.insert (idx);
            }
            previewBusy.store (false);
            juce::MessageManager::callAsync ([self] { if (self != nullptr) self->list.repaint(); });
        });
    }

public:
    static constexpr const char* previewBase = "https://raw.githubusercontent.com/Nine-Hands-Dsp/FM-9/main/catalog/previews/";
    static juce::Image galleryPreview (const CatalogEntry& e, bool local)
    {
        const auto cache = previewFile (e);
        if (cache.existsAsFile()) if (auto img = juce::ImageFileFormat::loadFrom (cache); img.isValid()) return img;
        juce::MemoryBlock data; juce::String err;
        if (FirmwareCatalog::download (previewBase + juce::URL::addEscapeChars (e.fileName() + ".png", false), data, err))
            if (auto img = juce::ImageFileFormat::loadFrom (data.getData(), data.getSize()); img.isValid())
            {
                cache.getParentDirectory().createDirectory();
                cache.replaceWithData (data.getData(), data.getSize());
                return img;
            }
        return local ? makePreview (e) : juce::Image();      // firmware installe : son ecran calcule ici
    }
private:

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
    juce::TextEditor search;
    int filterMode = 0;                // 0 tous, 1 compatibles, 2 installes, 3 favoris, 4 nouveautes, 3 favoris, 4 nouveautes
    int shownCount = 0;
    bool closeWhenDone = false;
    int hover = -1, current = -1, total = 0;
    std::atomic<int> done { 0 }, refreshed { 0 };
    std::atomic<float> installProg { 0.0f }, loadProg { 0.0f };
    int loadingIdx = -1;
    std::thread loadThread;
    bool busy = false;
};
} // namespace fm9
