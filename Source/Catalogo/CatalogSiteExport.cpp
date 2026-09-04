#include "CatalogSiteExport.h"

#include "../Ingest/Miniaturas.h"
#include "../Ingest/ProcessoExterno.h"
#include "../Vault/Resolucao.h"

namespace matriz::catalogo {

juce::String gerarSlugColecao(const juce::String& nomeOriginal) {
    juce::String s = nomeOriginal.toLowerCase().trim();
    juce::String out;
    out.preallocateBytes(static_cast<size_t>(s.length() + 8));

    for (int i = 0; i < s.length(); ++i) {
        juce::juce_wchar c = s[i];
        // Handle common latin accented characters
        if (c == L'á' || c == L'à' || c == L'ã' || c == L'â' || c == L'ä') c = 'a';
        else if (c == L'é' || c == L'è' || c == L'ê' || c == L'ë') c = 'e';
        else if (c == L'í' || c == L'ì' || c == L'î' || c == L'ï') c = 'i';
        else if (c == L'ó' || c == L'ò' || c == L'õ' || c == L'ô' || c == L'ö') c = 'o';
        else if (c == L'ú' || c == L'ù' || c == L'û' || c == L'ü') c = 'u';
        else if (c == L'ç') c = 'c';
        else if (c == L'ñ') c = 'n';

        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            out += static_cast<char>(c);
        } else if (c == ' ' || c == '_' || c == '-' || c == '/' || c == '.') {
            if (out.isNotEmpty() && out.getLastCharacter() != '-') {
                out += '-';
            }
        }
    }

    while (out.endsWithChar('-')) out = out.dropLastCharacters(1);
    while (out.startsWithChar('-')) out = out.substring(1);

    if (out.isEmpty()) out = "collection";
    return out;
}

namespace {

static juce::String formatarDuracao(double seg) {
    if (seg <= 0.0) return {};
    int totalSeg = static_cast<int>(std::round(seg));
    int h = totalSeg / 3600;
    int m = (totalSeg % 3600) / 60;
    int s = totalSeg % 60;
    if (h > 0) {
        return juce::String::formatted("%02d:%02d:%02d", h, m, s);
    }
    return juce::String::formatted("%02d:%02d", m, s);
}

static juce::File resolverArquivoMaster(const juce::File& pastaProjeto, const ui::ItemResumo& item) {
    if (!item.caminhoAbsolutoOrigem.empty()) {
        juce::File f(item.caminhoAbsolutoOrigem);
        if (f.existsAsFile()) return f;
    }
    if (!item.caminhoRelativoArquivo.empty()) {
        juce::File f = pastaProjeto.getChildFile(item.caminhoRelativoArquivo);
        if (f.existsAsFile()) return f;
    }
    return {};
}

static bool previewValidoEmCache(const juce::File& master, const juce::File& target) {
    if (!target.existsAsFile() || target.getSize() == 0) return false;
    if (!master.existsAsFile()) return true; // keep existing target if master is offline
    return (master.getLastModificationTime() <= target.getLastModificationTime());
}

static bool gerarPreviewAudio(const juce::File& master, const juce::File& dstPreview, int /*durationSec*/) {
    if (previewValidoEmCache(master, dstPreview)) return true;
    if (!master.existsAsFile()) return false;

    dstPreview.getParentDirectory().createDirectory();

    // If already web-standard audio, copy directly
    juce::String ext = master.getFileExtension().toLowerCase();
    if (ext == ".mp3" || ext == ".m4a") {
        if (master.copyFileTo(dstPreview)) return true;
    }

    juce::String ffmpeg = matriz::ingest::resolverCaminhoExecutavel("ffmpeg");
    juce::StringArray args;
    args.add(ffmpeg);
    args.add("-y");
    args.add("-hide_banner");
    args.add("-loglevel"); args.add("error");
    args.add("-i"); args.add(master.getFullPathName());
    args.add("-vn");
    args.add("-c:a"); args.add("aac");
    args.add("-b:a"); args.add("192k");
    args.add("-ar"); args.add("44100");
    args.add(dstPreview.getFullPathName());

    juce::ChildProcess proc;
    if (proc.start(args) && proc.waitForProcessToFinish(180000)) {
        return (proc.getExitCode() == 0 && dstPreview.existsAsFile() && dstPreview.getSize() > 0);
    }
    return false;
}

static bool gerarPreviewVideo(const juce::File& master, const juce::File& dstPreview, int /*durationSec*/) {
    if (previewValidoEmCache(master, dstPreview)) return true;
    if (!master.existsAsFile()) return false;

    dstPreview.getParentDirectory().createDirectory();

    juce::String ext = master.getFileExtension().toLowerCase();
    juce::String ffmpeg = matriz::ingest::resolverCaminhoExecutavel("ffmpeg");
    juce::StringArray args;
    args.add(ffmpeg);
    args.add("-y");
    args.add("-hide_banner");
    args.add("-loglevel"); args.add("error");
    args.add("-i"); args.add(master.getFullPathName());
    args.add("-vf"); args.add("scale=1280:720:force_original_aspect_ratio=decrease,pad=ceil(iw/2)*2:ceil(ih/2)*2");
    args.add("-c:v"); args.add("libx264");
    args.add("-preset"); args.add("fast");
    args.add("-crf"); args.add("24");
    args.add("-c:a"); args.add("aac");
    args.add("-b:a"); args.add("128k");
    args.add("-movflags"); args.add("+faststart");
    args.add(dstPreview.getFullPathName());

    juce::ChildProcess proc;
    if (proc.start(args) && proc.waitForProcessToFinish(300000)) {
        return (proc.getExitCode() == 0 && dstPreview.existsAsFile() && dstPreview.getSize() > 0);
    }
    return false;
}

static bool gerarThumbnailWeb(const juce::File& master,
                              const juce::File& pastaProjeto,
                              const ui::ItemResumo& item,
                              const juce::File& dstThumb,
                              int ladoMaximoPx) {
    if (previewValidoEmCache(master, dstThumb)) return true;

    dstThumb.getParentDirectory().createDirectory();
    std::string cat = item.tipoMidia;

    // 1. Try from master if master exists
    if (master.existsAsFile()) {
        if (cat == "imagem" || cat == "foto" || cat == "art" || cat == "artwork") {
            try {
                matriz::ingest::gerarMiniaturaImagem(master, dstThumb, ladoMaximoPx);
                if (dstThumb.existsAsFile() && dstThumb.getSize() > 0) return true;
            } catch (...) {}
        } else if (cat == "video" || cat == "filme" || cat == "clipe") {
            juce::String ffmpeg = matriz::ingest::resolverCaminhoExecutavel("ffmpeg");
            juce::StringArray args;
            args.add(ffmpeg);
            args.add("-y");
            args.add("-hide_banner");
            args.add("-loglevel"); args.add("error");
            args.add("-ss"); args.add("0.5");
            args.add("-i"); args.add(master.getFullPathName());
            args.add("-frames:v"); args.add("1");
            args.add("-vf"); args.add("scale=" + juce::String(ladoMaximoPx) + ":-2");
            args.add("-q:v"); args.add("4");
            args.add(dstThumb.getFullPathName());

            juce::ChildProcess proc;
            if (proc.start(args) && proc.waitForProcessToFinish(15000)) {
                if (dstThumb.existsAsFile() && dstThumb.getSize() > 0) return true;
            }
        } else if (cat == "audio" || cat == "faixa" || cat == "musica" || cat == "stem") {
            // Try extracting embedded cover art from audio file
            juce::String ffmpeg = matriz::ingest::resolverCaminhoExecutavel("ffmpeg");
            juce::StringArray args;
            args.add(ffmpeg);
            args.add("-y");
            args.add("-hide_banner");
            args.add("-loglevel"); args.add("error");
            args.add("-i"); args.add(master.getFullPathName());
            args.add("-an");
            args.add("-vcodec"); args.add("copy");
            args.add(dstThumb.getFullPathName());

            juce::ChildProcess proc;
            if (proc.start(args) && proc.waitForProcessToFinish(10000)) {
                if (dstThumb.existsAsFile() && dstThumb.getSize() > 0) return true;
            }
        }
    }

    // 2. Fallback: check cached thumbnail in collection project
    if (!item.miniaturaCaminhoRelativo.empty()) {
        juce::File cached = pastaProjeto.getChildFile(item.miniaturaCaminhoRelativo);
        if (cached.existsAsFile()) {
            if (cached.copyFileTo(dstThumb)) return true;
        }
    }

    // 3. Fallback: check .indice-cache/miniaturas/
    juce::File cacheMin = pastaProjeto.getChildFile(".indice-cache")
                                      .getChildFile("miniaturas")
                                      .getChildFile(item.id + ".jpg");
    if (cacheMin.existsAsFile()) {
        if (cacheMin.copyFileTo(dstThumb)) return true;
    }

    return false;
}

static const char* kCssTemplate = R"CSS(
/* ==========================================================================
   BKR MATRIZ — CATALOG HTML BROWSER
   Unified Touch-First Theme (Dark, Minimalist)
   ========================================================================== */

:root {
  --bg-canvas: #090b0e;
  --bg-surface: #12151b;
  --bg-surface-elevated: #181d26;
  --bg-glass: rgba(18, 21, 27, 0.88);
  --border-subtle: rgba(255, 255, 255, 0.08);
  --border-focus: rgba(226, 179, 87, 0.6);
  --accent: #e2b357;
  --accent-soft: rgba(226, 179, 87, 0.15);
  --text-primary: #f8fafc;
  --text-secondary: #94a3b8;
  --text-tertiary: #64748b;
  --radius-sm: 8px;
  --radius-md: 14px;
  --radius-lg: 22px;
  --font-serif: "Cinzel", "Playfair Display", "Georgia", serif;
  --font-sans: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "Helvetica Neue", Arial, sans-serif;
  --transition-smooth: all 0.3s cubic-bezier(0.16, 1, 0.3, 1);
}

*, *::before, *::after {
  box-sizing: border-box;
  margin: 0;
  padding: 0;
  touch-action: manipulation;
  -webkit-tap-highlight-color: transparent;
}

html, body {
  background-color: var(--bg-canvas);
  color: var(--text-primary);
  font-family: var(--font-sans);
  min-height: 100vh;
  overflow-x: hidden;
  user-select: none;
  -webkit-user-select: none;
}

body.has-custom-bg {
  background-size: cover;
  background-position: center;
  background-attachment: fixed;
  background-repeat: no-repeat;
}

body.idle-hide-cursor, body.idle-hide-cursor * {
  cursor: none !important;
}

/* Splash Screen (Single Gesture / Autoplay Unlock / Fullscreen) */
#splash-screen {
  position: fixed;
  inset: 0;
  z-index: 2000;
  background: var(--bg-canvas);
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  text-align: center;
  padding: 2rem;
  transition: opacity 0.5s cubic-bezier(0.16, 1, 0.3, 1), visibility 0.5s;
}
#splash-screen.hidden {
  opacity: 0;
  visibility: hidden;
  pointer-events: none;
}
.splash-content {
  max-width: 600px;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 1.5rem;
}
.splash-logo {
  max-height: 90px;
  max-width: 260px;
  object-fit: contain;
}
.splash-tagline {
  font-size: 0.85rem;
  text-transform: uppercase;
  letter-spacing: 0.3em;
  color: var(--accent);
  font-weight: 600;
}
.splash-title {
  font-family: var(--font-serif);
  font-size: 2.8rem;
  font-weight: 600;
  color: var(--text-primary);
  letter-spacing: 0.05em;
  line-height: 1.2;
}
.kiosk-enter-btn {
  margin-top: 1.5rem;
  background: var(--accent);
  color: #090b0e;
  border: none;
  border-radius: 9999px;
  padding: 1.2rem 3.5rem;
  font-size: 1.2rem;
  font-weight: 700;
  letter-spacing: 0.1em;
  text-transform: uppercase;
  cursor: pointer;
  box-shadow: 0 0 30px rgba(226, 179, 87, 0.35);
  transition: var(--transition-smooth);
}
.kiosk-enter-btn:active {
  transform: scale(0.95) !important;
  box-shadow: 0 0 45px rgba(226, 179, 87, 0.6) !important;
}

/* Floating Persistent Home Button */
.floating-home-btn {
  position: fixed;
  top: 1.5rem;
  right: 1.5rem;
  z-index: 1050;
  width: 48px;
  height: 48px;
  border-radius: 50%;
  background: var(--bg-surface-elevated);
  border: 1px solid var(--border-subtle);
  color: var(--text-primary);
  display: flex;
  align-items: center;
  justify-content: center;
  box-shadow: 0 4px 16px rgba(0,0,0,0.5);
  text-decoration: none;
  cursor: pointer;
  transition: var(--transition-smooth);
}
.floating-home-btn:hover, .floating-home-btn:active {
  border-color: var(--accent);
  color: var(--accent);
  transform: scale(1.06);
}

/* Base Container */
.container {
  max-width: 1400px;
  margin: 0 auto;
  padding: 2.5rem 2rem;
}

/* Header & Catalog Branding */
.catalog-header {
  border-bottom: 1px solid var(--border-subtle);
  padding: 1.5rem 0 2rem 0;
  margin-bottom: 2.5rem;
  display: flex;
  justify-content: space-between;
  align-items: center;
  flex-wrap: wrap;
  gap: 1.5rem;
}

.catalog-brand-header-wrap {
  display: flex;
  align-items: center;
  gap: 1.25rem;
  flex-wrap: wrap;
}

.catalog-logo {
  max-height: 56px;
  max-width: 180px;
  object-fit: contain;
  border-radius: var(--radius-sm);
}

.catalog-brand {
  display: flex;
  flex-direction: column;
  gap: 0.25rem;
}

.catalog-tagline {
  font-size: 0.8rem;
  text-transform: uppercase;
  letter-spacing: 0.25em;
  color: var(--accent);
  font-weight: 600;
}

.catalog-title {
  font-family: var(--font-serif);
  font-size: 2.2rem;
  font-weight: 500;
  letter-spacing: 0.05em;
  color: var(--text-primary);
}

.catalog-stats {
  display: flex;
  gap: 1.5rem;
}

.stat-pill {
  background: var(--bg-surface);
  border: 1px solid var(--border-subtle);
  border-radius: 9999px;
  padding: 0.5rem 1.25rem;
  font-size: 0.85rem;
  color: var(--text-secondary);
}
.stat-pill strong {
  color: var(--text-primary);
}

/* Top Navigation Bar */
.nav-bar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 2rem;
  flex-wrap: wrap;
  gap: 1rem;
}

.nav-back-btn {
  display: inline-flex;
  align-items: center;
  gap: 0.6rem;
  background: var(--bg-surface);
  color: var(--text-primary);
  border: 1px solid var(--border-subtle);
  border-radius: var(--radius-sm);
  padding: 0.85rem 1.5rem;
  font-size: 0.95rem;
  font-weight: 500;
  text-decoration: none;
  cursor: pointer;
  min-height: 48px;
  transition: var(--transition-smooth);
}
.nav-back-btn:active {
  background: var(--bg-surface-elevated);
  border-color: var(--accent);
}

/* Filters & Search Toolbar */
.filter-toolbar {
  display: flex;
  align-items: center;
  gap: 0.75rem;
  flex-wrap: wrap;
}

.filter-btn {
  background: var(--bg-surface);
  border: 1px solid var(--border-subtle);
  color: var(--text-secondary);
  border-radius: var(--radius-sm);
  padding: 0.7rem 1.2rem;
  font-size: 0.9rem;
  font-weight: 500;
  cursor: pointer;
  min-height: 46px;
  min-width: 46px;
  transition: var(--transition-smooth);
}
.filter-btn.active {
  background: var(--accent);
  color: #090b0e;
  border-color: var(--accent);
  font-weight: 600;
}

.year-select {
  background: var(--bg-surface);
  border: 1px solid var(--border-subtle);
  color: var(--text-secondary);
  border-radius: var(--radius-sm);
  padding: 0.7rem 1.2rem;
  font-size: 0.9rem;
  font-weight: 500;
  cursor: pointer;
  min-height: 46px;
  outline: none;
  transition: var(--transition-smooth);
}
.year-select:focus, .year-select:active {
  border-color: var(--accent);
  color: var(--text-primary);
}

.search-box {
  position: relative;
  min-width: 220px;
}
.search-input {
  width: 100%;
  background: var(--bg-surface);
  border: 1px solid var(--border-subtle);
  border-radius: var(--radius-sm);
  padding: 0.75rem 1.25rem;
  color: var(--text-primary);
  font-size: 0.95rem;
  min-height: 48px;
}
.search-input:focus {
  outline: none;
  border-color: var(--accent);
}

/* Collection Hub Showcase Cards */
.collections-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(360px, 1fr));
  gap: 2.5rem;
  margin-top: 1.5rem;
}

.collection-card {
  background: var(--bg-surface);
  border: 1px solid var(--border-subtle);
  border-radius: var(--radius-md);
  overflow: hidden;
  cursor: pointer;
  text-decoration: none;
  color: inherit;
  display: flex;
  flex-direction: column;
  transition: var(--transition-smooth);
}
.collection-card:active {
  transform: scale(0.96);
  border-color: var(--accent);
  box-shadow: 0 0 20px var(--accent-soft);
}

.collection-cover-box {
  width: 100%;
  aspect-ratio: 16 / 10;
  background: #000;
  overflow: hidden;
  position: relative;
  display: flex;
  align-items: center;
  justify-content: center;
}

.collection-cover-img {
  width: 100%;
  height: 100%;
  object-fit: cover;
}

.collection-cover-placeholder {
  width: 100%;
  height: 100%;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  color: var(--text-tertiary);
  gap: 0.75rem;
}

.collection-info {
  padding: 1.75rem;
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
}

.collection-group-tag {
  font-size: 0.75rem;
  letter-spacing: 0.15em;
  text-transform: uppercase;
  color: var(--accent);
  font-weight: 600;
}

.collection-name {
  font-family: var(--font-serif);
  font-size: 1.4rem;
  color: var(--text-primary);
  font-weight: 500;
}

.collection-count {
  font-size: 0.9rem;
  color: var(--text-secondary);
  margin-top: 0.25rem;
}

/* Asset Grid (Spacious, Large Items) */
.asset-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(320px, 1fr));
  gap: 2.25rem;
  margin-top: 1.5rem;
}

.asset-card {
  background: var(--bg-surface);
  border: 1px solid var(--border-subtle);
  border-radius: var(--radius-md);
  overflow: hidden;
  cursor: pointer;
  display: flex;
  flex-direction: column;
  transition: var(--transition-smooth);
}
.asset-card:active {
  transform: scale(0.96);
  border-color: var(--accent);
  box-shadow: 0 0 20px var(--accent-soft);
}

.asset-thumb-wrap {
  width: 100%;
  aspect-ratio: 1 / 1;
  background: #000;
  overflow: hidden;
  position: relative;
  display: flex;
  align-items: center;
  justify-content: center;
}

.asset-thumb-img {
  width: 100%;
  height: 100%;
  object-fit: cover;
}

.asset-type-badge {
  position: absolute;
  top: 1rem;
  right: 1rem;
  background: rgba(0, 0, 0, 0.75);
  backdrop-filter: blur(8px);
  border: 1px solid rgba(255, 255, 255, 0.15);
  color: var(--accent);
  border-radius: var(--radius-sm);
  padding: 0.3rem 0.65rem;
  font-size: 0.75rem;
  font-weight: 600;
  text-transform: uppercase;
  letter-spacing: 0.05em;
}

.asset-body {
  padding: 1.25rem 1.5rem 1.5rem 1.5rem;
  display: flex;
  flex-direction: column;
  gap: 0.4rem;
}

.asset-title {
  font-size: 1.1rem;
  color: var(--text-primary);
  font-weight: 500;
  line-height: 1.3;
}

.asset-artist {
  font-size: 0.9rem;
  color: var(--text-secondary);
}

.asset-meta-row {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  font-size: 0.8rem;
  color: var(--text-tertiary);
  margin-top: 0.25rem;
}

/* Fullscreen Detail View Modal */
.modal-backdrop {
  position: fixed;
  inset: 0;
  background: rgba(4, 5, 7, 0.96);
  backdrop-filter: blur(20px);
  z-index: 1000;
  display: none;
  align-items: center;
  justify-content: center;
  padding: 2rem;
}
.modal-backdrop.open {
  display: flex;
}

.detail-stage {
  width: 100%;
  max-width: 1200px;
  max-height: 92vh;
  display: flex;
  background: var(--bg-surface);
  border: 1px solid var(--border-subtle);
  border-radius: var(--radius-lg);
  overflow: hidden;
  position: relative;
  box-shadow: 0 25px 60px rgba(0, 0, 0, 0.8);
}

.detail-close-btn {
  position: absolute;
  top: 1.5rem;
  right: 1.5rem;
  width: 52px;
  height: 52px;
  border-radius: 50%;
  background: var(--bg-surface-elevated);
  border: 1px solid var(--border-subtle);
  color: var(--text-primary);
  font-size: 1.5rem;
  display: flex;
  align-items: center;
  justify-content: center;
  cursor: pointer;
  z-index: 20;
  transition: var(--transition-smooth);
}
.detail-close-btn:active {
  background: var(--accent);
  color: #090b0e;
}

.detail-nav-btn {
  position: absolute;
  top: 50%;
  transform: translateY(-50%);
  width: 56px;
  height: 72px;
  background: rgba(18, 21, 27, 0.85);
  border: 1px solid var(--border-subtle);
  color: var(--text-primary);
  font-size: 2rem;
  display: flex;
  align-items: center;
  justify-content: center;
  cursor: pointer;
  z-index: 15;
  transition: var(--transition-smooth);
}
.detail-nav-btn.prev {
  left: 0;
  border-radius: 0 var(--radius-md) var(--radius-md) 0;
}
.detail-nav-btn.next {
  right: 0;
  border-radius: var(--radius-md) 0 0 var(--radius-md);
}
.detail-nav-btn:active {
  background: var(--accent);
  color: #090b0e;
}

.detail-visual {
  flex: 1.3;
  background: #000;
  display: flex;
  align-items: center;
  justify-content: center;
  overflow: hidden;
  position: relative;
  min-height: 480px;
}

.detail-media-img {
  width: 100%;
  height: 100%;
  object-fit: contain;
}

.detail-video-elem {
  width: 100%;
  height: 100%;
  outline: none;
}

.detail-info-panel {
  flex: 1;
  padding: 3rem;
  display: flex;
  flex-direction: column;
  justify-content: space-between;
  overflow-y: auto;
  gap: 2rem;
}

/* Automatic Audio Playing Indicator */
.audio-playing-indicator {
  background: var(--bg-surface-elevated);
  border: 1px solid var(--border-subtle);
  border-radius: var(--radius-md);
  padding: 1rem 1.25rem;
  display: flex;
  align-items: center;
  gap: 0.85rem;
  margin: 1.25rem 0;
  color: var(--accent);
  font-size: 0.95rem;
  font-weight: 500;
}

.audio-pulse-dot {
  width: 12px;
  height: 12px;
  border-radius: 50%;
  background: var(--accent);
  box-shadow: 0 0 12px var(--accent);
  animation: audioPulse 1.4s infinite ease-in-out;
}

@keyframes audioPulse {
  0% { transform: scale(0.85); opacity: 0.5; }
  50% { transform: scale(1.25); opacity: 1; }
  100% { transform: scale(0.85); opacity: 0.5; }
}
}

.meta-field-group {
  display: grid;
  grid-template-columns: repeat(2, 1fr);
  gap: 1.5rem;
  margin-top: 1.5rem;
}
.meta-field {
  display: flex;
  flex-direction: column;
  gap: 0.25rem;
}

/* Pagination Footer */
.pagination-wrap {
  margin-top: 3rem;
  display: flex;
  justify-content: center;
  gap: 1rem;
}
.page-btn {
  background: var(--bg-surface);
  border: 1px solid var(--border-subtle);
  color: var(--text-primary);
  border-radius: var(--radius-sm);
  padding: 0.75rem 1.5rem;
  font-size: 0.95rem;
  min-height: 48px;
  cursor: pointer;
}
.page-btn:disabled {
  opacity: 0.4;
  cursor: not-allowed;
}

/* Tactile Active Feedback */
.asset-card:active, .collection-card:active, .filter-btn:active, .nav-back-btn:active, .play-pause-btn:active, .page-btn:active, .year-select:active {
  transform: scale(0.96) !important;
  border-color: var(--accent) !important;
  box-shadow: 0 0 16px var(--accent-soft) !important;
  transition: transform 180ms cubic-bezier(0.16, 1, 0.3, 1), border-color 180ms, box-shadow 180ms !important;
}
)CSS";

static const char* kJsTemplate = R"JS(
/**
 * BKR MATRIZ — CATALOG HTML BROWSER
 * Pure Vanilla JS, zero-dependency, touch-optimized single interface.
 */

(function () {
  'use strict';

  let catalogManifest = null;
  let currentCollection = null;
  let currentAssets = [];
  let filteredAssets = [];
  let currentAssetIndex = -1;
  let activeFilter = 'all';
  let activeYear = 'all';
  let searchTerm = '';
  let currentPage = 1;
  let totalPages = 1;
  let cursorInactivityTimer = null;
  let autoReturnTimer = null;

  window.__allowExitFullscreen__ = false;

  // Audio preview element
  const audioPreview = new Audio();

  // DOM Elements
  const modal = document.getElementById('detail-modal');
  const modalCloseBtn = document.getElementById('modal-close-btn');
  const modalPrevBtn = document.getElementById('modal-prev-btn');
  const modalNextBtn = document.getElementById('modal-next-btn');

  // Swipe detection on modal
  let touchStartX = 0;
  let touchStartY = 0;

  function init() {
    setupInactivityCursorTimer();
    setup30sInactivityReturnTimer();
    setupFullscreenEnforcement();
    setupSplashScreen();
    setupModalControls();
    setupSwipeListeners();

    // Check if we are on landing or collection page
    const collectionSlug = document.body.dataset.collectionSlug;
    if (collectionSlug) {
      loadCollectionPage(collectionSlug);
    } else {
      loadCatalogHub();
    }
  }

  function enforceFullscreen() {
    if (window.__allowExitFullscreen__) return;
    if (!document.fullscreenElement && document.documentElement.requestFullscreen) {
      document.documentElement.requestFullscreen().catch(() => {});
    }
  }

  function setupFullscreenEnforcement() {
    // Attempt fullscreen on load and on any interaction
    enforceFullscreen();
    window.addEventListener('click', enforceFullscreen, { passive: true });
    window.addEventListener('touchstart', enforceFullscreen, { passive: true });

    // Exit fullscreen ONLY if Command+Shift+Escape (or Ctrl+Shift+Escape) is pressed
    window.addEventListener('keydown', (e) => {
      const isCmdOrCtrl = e.metaKey || e.ctrlKey;
      const isShift = e.shiftKey;
      const isEsc = (e.key === 'Escape' || e.code === 'Escape' || e.keyCode === 27);

      if (isCmdOrCtrl && isShift && isEsc) {
        window.__allowExitFullscreen__ = true;
        if (document.exitFullscreen) {
          document.exitFullscreen().catch(() => {});
        }
        return;
      }

      // If user hits single Escape while modal is open, close modal only
      if (isEsc && !window.__allowExitFullscreen__) {
        if (modal && modal.classList.contains('open')) {
          closeModal();
        }
        e.preventDefault();
        enforceFullscreen();
      }
    }, true);

    document.addEventListener('fullscreenchange', () => {
      if (!document.fullscreenElement && !window.__allowExitFullscreen__) {
        setTimeout(enforceFullscreen, 250);
      }
    });
  }

  function setup30sInactivityReturnTimer() {
    function resetActivityTimers() {
      // 1. Hide cursor after 3.5s idle
      document.body.classList.remove('idle-hide-cursor');
      if (cursorInactivityTimer) clearTimeout(cursorInactivityTimer);
      cursorInactivityTimer = setTimeout(() => {
        document.body.classList.add('idle-hide-cursor');
      }, 3500);

      // 2. Return to initial splash screen after 30s of total inactivity
      if (autoReturnTimer) clearTimeout(autoReturnTimer);
      autoReturnTimer = setTimeout(() => {
        closeModal();
        const isSubpage = !!document.body.dataset.collectionSlug;
        if (isSubpage) {
          window.location.href = '../../index.html';
        } else {
          const splash = document.getElementById('splash-screen');
          if (splash) {
            splash.classList.remove('hidden');
          }
        }
      }, 30000);
    }

    window.addEventListener('mousemove', resetActivityTimers, { passive: true });
    window.addEventListener('mousedown', resetActivityTimers, { passive: true });
    window.addEventListener('touchstart', resetActivityTimers, { passive: true });
    window.addEventListener('keydown', resetActivityTimers, { passive: true });
    window.addEventListener('scroll', resetActivityTimers, { passive: true });
    window.addEventListener('click', resetActivityTimers, { passive: true });

    resetActivityTimers();
  }

  function setupSplashScreen() {
    const enterBtn = document.getElementById('btn-enter-kiosk');
    const splash = document.getElementById('splash-screen');
    if (enterBtn && splash) {
      enterBtn.addEventListener('click', () => {
        window.__allowExitFullscreen__ = false;
        enforceFullscreen();
        splash.classList.add('hidden');
        try {
          const dummyAudio = new Audio();
          dummyAudio.play().catch(() => {});
        } catch (e) {}
      });
    }
  }

  function setupInactivityCursorTimer() {
    // Managed in setup30sInactivityReturnTimer
  }

  function setupModalControls() {
    if (modalCloseBtn) modalCloseBtn.addEventListener('click', closeModal);
    if (modalPrevBtn) modalPrevBtn.addEventListener('click', showPreviousAsset);
    if (modalNextBtn) modalNextBtn.addEventListener('click', showNextAsset);

    if (modal) {
      modal.addEventListener('click', (e) => {
        if (e.target === modal) closeModal();
      });
    }

    // Arrow navigation support
    window.addEventListener('keydown', (e) => {
      if (!modal || !modal.classList.contains('open')) return;
      if (e.key === 'ArrowLeft') showPreviousAsset();
      else if (e.key === 'ArrowRight') showNextAsset();
    });
  }

  function setupSwipeListeners() {
    if (!modal) return;

    modal.addEventListener('touchstart', (e) => {
      touchStartX = e.changedTouches[0].screenX;
      touchStartY = e.changedTouches[0].screenY;
    }, { passive: true });

    modal.addEventListener('touchend', (e) => {
      const deltaX = e.changedTouches[0].screenX - touchStartX;
      const deltaY = e.changedTouches[0].screenY - touchStartY;
      if (Math.abs(deltaX) > 60 && Math.abs(deltaX) > Math.abs(deltaY) * 1.5) {
        if (deltaX > 0) {
          showPreviousAsset();
        } else {
          showNextAsset();
        }
      }
    }, { passive: true });
  }

  async function loadCatalogHub() {
    try {
      let data = window.__CATALOG_MANIFEST__;
      if (!data) {
        try {
          const res = await fetch('manifest.json');
          if (res.ok) data = await res.json();
        } catch (err) {}
      }
      if (data) {
        catalogManifest = data;
        renderCatalogHub(data);
      }
    } catch (e) {
      console.error('Error loading manifest:', e);
    }
  }

  function renderCatalogHub(data) {
    const titleEl = document.getElementById('catalog-title');
    if (titleEl && data.catalogName) titleEl.textContent = data.catalogName;

    const countColEl = document.getElementById('stat-collections-count');
    if (countColEl) countColEl.textContent = data.totalCollections !== undefined ? data.totalCollections : (data.collections ? data.collections.length : 0);

    const countAssetEl = document.getElementById('stat-assets-count');
    if (countAssetEl && data.totalAssets !== undefined) countAssetEl.textContent = data.totalAssets;

    const grid = document.getElementById('collections-grid');
    if (!grid) return;

    grid.innerHTML = '';
    (data.collections || []).forEach(col => {
      const card = document.createElement('a');
      card.className = 'collection-card';
      card.href = `collections/${col.slug}/index.html`;

      let coverHtml = '';
      if (col.coverThumb) {
        coverHtml = `<img class="collection-cover-img" src="${col.coverThumb}" alt="${col.name}" loading="lazy">`;
      } else {
        coverHtml = `<div class="collection-cover-placeholder">
          <svg width="48" height="48" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"><rect x="3" y="3" width="18" height="18" rx="2"/><path d="M3 9h18M9 21V9"/></svg>
          <span>Collection Showcase</span>
        </div>`;
      }

      card.innerHTML = `
        <div class="collection-cover-box">${coverHtml}</div>
        <div class="collection-info">
          ${col.group ? `<span class="collection-group-tag">${col.group}</span>` : ''}
          <h2 class="collection-name">${col.name}</h2>
          <span class="collection-count">${col.assetCount} items</span>
        </div>
      `;
      grid.appendChild(card);
    });
  }

  async function loadCollectionPage(slug) {
    try {
      let data = window.__COLLECTION_DATA__;
      if (!data) {
        try {
          const res = await fetch('data.json');
          if (res.ok) data = await res.json();
        } catch (err) {}
      }
      if (data) {
        currentCollection = data.collection;
        currentAssets = data.assets || [];
        totalPages = data.totalPages || 1;
        currentPage = 1;

        setupCollectionToolbar();
        applyFiltersAndRender();
      }
    } catch (e) {
      console.error('Error loading collection data:', e);
    }
  }

  function setupCollectionToolbar() {
    const searchInput = document.getElementById('collection-search');
    if (searchInput) {
      searchInput.addEventListener('input', (e) => {
        searchTerm = e.target.value.toLowerCase().trim();
        applyFiltersAndRender();
      });
    }

    const filterBtns = document.querySelectorAll('.filter-btn');
    filterBtns.forEach(btn => {
      btn.addEventListener('click', () => {
        filterBtns.forEach(b => b.classList.remove('active'));
        btn.classList.add('active');
        activeFilter = btn.dataset.filter || 'all';
        applyFiltersAndRender();
      });
    });

    // Populate dynamic year filter
    const yearSelect = document.getElementById('year-filter');
    if (yearSelect) {
      yearSelect.innerHTML = '<option value="all">All Years</option>';
      const distinctYears = Array.from(
        new Set(
          currentAssets
            .map(a => a.year)
            .filter(y => y !== undefined && y !== null && y !== '' && Number(y) > 0)
        )
      ).sort((a, b) => Number(b) - Number(a));

      distinctYears.forEach(y => {
        const opt = document.createElement('option');
        opt.value = String(y);
        opt.textContent = String(y);
        yearSelect.appendChild(opt);
      });

      yearSelect.addEventListener('change', (e) => {
        activeYear = e.target.value;
        applyFiltersAndRender();
      });
    }
  }

  function applyFiltersAndRender() {
    filteredAssets = currentAssets.filter(item => {
      // Type filter
      if (activeFilter !== 'all') {
        const type = (item.mediaType || '').toLowerCase();
        if (activeFilter === 'audio' && !type.includes('audio') && !type.includes('musica') && !type.includes('faixa') && !type.includes('sound') && !type.includes('som') && !type.includes('fita') && !type.includes('vinil') && !type.includes('cassete')) return false;
        if (activeFilter === 'video' && !type.includes('video') && !type.includes('filme') && !type.includes('clipe')) return false;
        if (activeFilter === 'image' && !type.includes('imagem') && !type.includes('foto') && !type.includes('artwork') && !type.includes('cover') && !type.includes('slide') && !type.includes('negativo')) return false;
        if (activeFilter === 'doc' && !type.includes('documento') && !type.includes('doc') && !type.includes('texto') && !type.includes('pdf')) return false;
      }
      // Year filter
      if (activeYear !== 'all') {
        if (!item.year || String(item.year) !== String(activeYear)) return false;
      }
      // Search term filter
      if (searchTerm) {
        const fullText = `${item.title || ''} ${item.artist || ''} ${item.year || ''} ${item.code || ''} ${(item.tags || []).join(' ')}`.toLowerCase();
        if (!fullText.includes(searchTerm)) return false;
      }
      return true;
    });

    renderAssetGrid(filteredAssets);
  }

  function renderAssetGrid(assets) {
    const grid = document.getElementById('asset-grid');
    if (!grid) return;

    grid.innerHTML = '';
    if (assets.length === 0) {
      grid.innerHTML = '<div style="grid-column: 1/-1; text-align: center; padding: 4rem 1rem; color: var(--text-tertiary); font-size: 1.1rem;">No items found matching the selected filter.</div>';
      return;
    }

    assets.forEach((asset, idx) => {
      const card = document.createElement('div');
      card.className = 'asset-card';
      card.onclick = () => openDetailModal(idx);

      let thumbHtml = '';
      if (asset.thumbnail) {
        thumbHtml = `<img class="asset-thumb-img" src="${asset.thumbnail}" alt="${asset.title}" loading="lazy">`;
      } else {
        thumbHtml = `<div class="collection-cover-placeholder">
          <svg width="40" height="40" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"><circle cx="12" cy="12" r="10"/><polygon points="10 8 16 12 10 16 10 8"/></svg>
        </div>`;
      }

      const typeLabel = asset.mediaType || 'ASSET';

      card.innerHTML = `
        <div class="asset-thumb-wrap">
          ${thumbHtml}
          <span class="asset-type-badge">${typeLabel}</span>
        </div>
        <div class="asset-body">
          <h3 class="asset-title">${asset.title || 'Untitled'}</h3>
          ${asset.artist ? `<span class="asset-artist">${asset.artist}</span>` : ''}
          <div class="asset-meta-row">
            ${asset.year ? `<span>${asset.year}</span>` : ''}
            ${asset.durationFormatted ? `<span>• ${asset.durationFormatted}</span>` : ''}
            ${asset.contentType ? `<span>• ${asset.contentType}</span>` : ''}
          </div>
        </div>
      `;
      grid.appendChild(card);
    });
  }

  function openDetailModal(index) {
    if (index < 0 || index >= filteredAssets.length) return;
    currentAssetIndex = index;
    const asset = filteredAssets[index];

    // Stop previous audio & video
    stopMedia();

    // Populate visual stage
    const visualStage = document.getElementById('detail-visual-stage');
    if (visualStage) {
      visualStage.innerHTML = '';
      if (asset.previewVideo) {
        visualStage.innerHTML = `
          <video class="detail-video-elem" autoplay playsinline loop>
            <source src="${asset.previewVideo}" type="video/mp4">
            Your browser does not support video playback.
          </video>
        `;
        const vid = visualStage.querySelector('video');
        if (vid) vid.play().catch(() => {});
      } else if (asset.thumbnail) {
        visualStage.innerHTML = `<img class="detail-media-img" src="${asset.thumbnail}" alt="${asset.title}">`;
      } else {
        visualStage.innerHTML = `
          <div class="collection-cover-placeholder" style="color: var(--accent); font-size: 1.2rem;">
            <svg width="64" height="64" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"><circle cx="12" cy="12" r="10"/><path d="M12 8v8M8 12h8"/></svg>
            <span style="margin-top:1rem">${asset.mediaType || 'Master Asset'}</span>
          </div>
        `;
      }
    }

    // Populate info panel
    const titleEl = document.getElementById('detail-title');
    if (titleEl) titleEl.textContent = asset.title || 'Untitled';

    const artistEl = document.getElementById('detail-artist');
    if (artistEl) {
      artistEl.textContent = asset.artist || '';
      artistEl.style.display = asset.artist ? 'block' : 'none';
    }

    const codeEl = document.getElementById('detail-code');
    if (codeEl) codeEl.textContent = asset.code || asset.id || '';

    const yearEl = document.getElementById('detail-year');
    if (yearEl) yearEl.textContent = asset.year || '—';

    const typeEl = document.getElementById('detail-type');
    if (typeEl) typeEl.textContent = asset.mediaType || '—';

    const durationEl = document.getElementById('detail-duration');
    if (durationEl) durationEl.textContent = asset.durationFormatted || '—';

    const contentEl = document.getElementById('detail-content-type');
    if (contentEl) contentEl.textContent = asset.contentType || '—';

    // Tags
    const tagsContainer = document.getElementById('detail-tags');
    if (tagsContainer) {
      tagsContainer.innerHTML = '';
      if (asset.tags && asset.tags.length > 0) {
        asset.tags.forEach(t => {
          const chip = document.createElement('span');
          chip.className = 'tag-chip';
          chip.textContent = t;
          tagsContainer.appendChild(chip);
        });
      }
    }

    // Automatic Audio Playback without interactive play/stop controls
    const playerContainer = document.getElementById('detail-audio-player-wrap');
    if (playerContainer) {
      playerContainer.innerHTML = '';
      if (asset.previewAudio) {
        playerContainer.innerHTML = `
          <div class="audio-playing-indicator">
            <span class="audio-pulse-dot"></span>
            <span>Playing audio asset</span>
          </div>
        `;
        audioPreview.src = asset.previewAudio;
        audioPreview.currentTime = 0;
        audioPreview.play().catch(e => console.log('Audio autoplay:', e));
      }
    }

    if (modal) modal.classList.add('open');
  }

  function stopMedia() {
    audioPreview.pause();
    audioPreview.currentTime = 0;
    audioPreview.src = '';
    if (modal) {
      const vid = modal.querySelector('video');
      if (vid) {
        vid.pause();
        vid.src = '';
      }
    }
  }

  function closeModal() {
    stopMedia();
    const visualStage = document.getElementById('detail-visual-stage');
    if (visualStage) visualStage.innerHTML = '';
    if (modal) modal.classList.remove('open');
  }

  function showPreviousAsset() {
    if (currentAssetIndex > 0) {
      openDetailModal(currentAssetIndex - 1);
    } else if (filteredAssets.length > 0) {
      openDetailModal(filteredAssets.length - 1);
    }
  }

  function showNextAsset() {
    if (currentAssetIndex < filteredAssets.length - 1) {
      openDetailModal(currentAssetIndex + 1);
    } else if (filteredAssets.length > 0) {
      openDetailModal(0);
    }
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
  } else {
    init();
  }
})();
)JS";

static const char* kLandingHtmlTemplate = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
  <title>%CATALOG_NAME% &mdash; BKR Matriz</title>
  <link rel="stylesheet" href="css/style.css">
</head>
<body class="%BODY_CUSTOM_BG_CLASS%" %BODY_CUSTOM_BG_STYLE%>
  <!-- Splash / Entry Screen with single gesture (Kiosk / Autoplay unlock / Fullscreen) -->
  <div id="splash-screen">
    <div class="splash-content">
      %SPLASH_LOGO_HTML%
      <span class="splash-tagline">Permanent Archive &amp; Catalog Collection</span>
      <h1 class="splash-title">%CATALOG_NAME%</h1>
      <button id="btn-enter-kiosk" class="kiosk-enter-btn" aria-label="Enter Catalog">TAP TO BEGIN</button>
    </div>
  </div>

  <!-- Persistent Floating Home Button -->
  <a href="index.html" class="floating-home-btn" aria-label="Catalog Home">
    <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M3 9l9-7 9 7v11a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/><polyline points="9 22 9 12 15 12 15 22"/></svg>
  </a>

  <div class="container">
    <header class="catalog-header">
      <div class="catalog-brand-header-wrap">
        %CATALOG_LOGO_HTML%
        <div class="catalog-brand">
          <span class="catalog-tagline">Permanent Archive &amp; Catalog Collection</span>
          <h1 class="catalog-title" id="catalog-title">%CATALOG_NAME%</h1>
        </div>
      </div>
      <div class="catalog-stats">
        <div class="stat-pill"><strong id="stat-collections-count">%TOTAL_COLLECTIONS%</strong> Collections</div>
        <div class="stat-pill"><strong id="stat-assets-count">%TOTAL_ASSETS%</strong> Total Assets</div>
      </div>
    </header>

    <div class="collections-grid" id="collections-grid">
      <!-- Collection cards rendered via JS -->
    </div>
  </div>

  <script>window.__CATALOG_MANIFEST__ = %MANIFEST_JSON%;</script>
  <script src="js/app.js"></script>
</body>
</html>
)HTML";

static const char* kCollectionHtmlTemplate = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
  <title>%COLLECTION_NAME% &mdash; BKR Matriz</title>
  <link rel="stylesheet" href="../../css/style.css">
</head>
<body data-collection-slug="%COLLECTION_SLUG%" class="%BODY_CUSTOM_BG_CLASS%" %BODY_CUSTOM_BG_STYLE%>
  <!-- Persistent Floating Home Button -->
  <a href="../../index.html" class="floating-home-btn" aria-label="Return to Catalog Home">
    <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M3 9l9-7 9 7v11a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/><polyline points="9 22 9 12 15 12 15 22"/></svg>
  </a>

  <div class="container">
    <div class="nav-bar">
      <a href="../../index.html" class="nav-back-btn" aria-label="Return to Catalog Hub">
        <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M19 12H5M12 19l-7-7 7-7"/></svg>
        <span>Back to Collections</span>
      </a>

      <div class="filter-toolbar">
        <div class="search-box">
          <input type="text" id="collection-search" class="search-input" placeholder="Search collection...">
        </div>
        <button class="filter-btn active" data-filter="all">All</button>
        <button class="filter-btn" data-filter="audio">Audio</button>
        <button class="filter-btn" data-filter="video">Video</button>
        <button class="filter-btn" data-filter="image">Images</button>
        <button class="filter-btn" data-filter="doc">Documents</button>
        <select id="year-filter" class="year-select" aria-label="Filter by year">
          <option value="all">All Years</option>
        </select>
      </div>
    </div>

    <header class="catalog-header">
      <div class="catalog-brand-header-wrap">
        %CATALOG_LOGO_HTML%
        <div class="catalog-brand">
          <span class="catalog-tagline">%COLLECTION_GROUP%</span>
          <h1 class="catalog-title">%COLLECTION_NAME%</h1>
        </div>
      </div>
      <div class="catalog-stats">
        <div class="stat-pill"><strong>%COLLECTION_TOTAL_ASSETS%</strong> Assets in Collection</div>
      </div>
    </header>

    <div class="asset-grid" id="asset-grid">
      <!-- Assets rendered via JS -->
    </div>
  </div>

  <!-- Fullscreen Contemplative Detail View Modal -->
  <div class="modal-backdrop" id="detail-modal" role="dialog" aria-modal="true">
    <div class="detail-stage">
      <button class="detail-close-btn" id="modal-close-btn" aria-label="Close detail view">&times;</button>
      <button class="detail-nav-btn prev" id="modal-prev-btn" aria-label="Previous item">&#8249;</button>
      <button class="detail-nav-btn next" id="modal-next-btn" aria-label="Next item">&#8250;</button>

      <div class="detail-visual" id="detail-visual-stage"></div>

      <div class="detail-info-panel">
        <div>
          <span class="catalog-tagline" id="detail-code">BKR-0000</span>
          <h2 class="catalog-title" id="detail-title" style="font-size: 1.8rem; margin: 0.5rem 0;">Untitled</h2>
          <span class="asset-artist" id="detail-artist" style="font-size: 1.15rem;"></span>

          <div id="detail-audio-player-wrap"></div>

          <div class="meta-field-group">
            <div class="meta-field">
              <span class="meta-label">Year</span>
              <span class="meta-value" id="detail-year">&mdash;</span>
            </div>
            <div class="meta-field">
              <span class="meta-label">Media Type</span>
              <span class="meta-value" id="detail-type">&mdash;</span>
            </div>
            <div class="meta-field">
              <span class="meta-label">Duration</span>
              <span class="meta-value" id="detail-duration">&mdash;</span>
            </div>
            <div class="meta-field">
              <span class="meta-label">Content</span>
              <span class="meta-value" id="detail-content-type">&mdash;</span>
            </div>
          </div>

          <div class="tags-row" id="detail-tags"></div>
        </div>
      </div>
    </div>
  </div>

  <script>window.__COLLECTION_DATA__ = %COLLECTION_DATA_JSON%;</script>
  <script src="../../js/app.js"></script>
</body>
</html>
)HTML";

} // namespace

ResultadoExportSite exportarHtmlBrowser(ui::ProjetoAberto& projeto,
                                       const juce::File& destino,
                                       const ParamsExportSite& params,
                                       const matriz::consolidacao::AoProgredir& aoProgredir) {
    ResultadoExportSite resultado;
    resultado.outputDirectory = destino;

    if (projeto.projeto().modo() != matriz::model::Modo::Catalogo) {
        resultado.errors.push_back("Export HTML Browser is only available for CATALOG projects.");
        return resultado;
    }

    auto colecoes = projeto.listarColecoesLinkadas();
    if (colecoes.empty()) {
        resultado.errors.push_back("No linked collections found in this catalog.");
        return resultado;
    }

    destino.createDirectory();
    juce::File dirMedia = destino.getChildFile("media");
    juce::File dirThumbs = dirMedia.getChildFile("thumbs");
    juce::File dirPreviews = dirMedia.getChildFile("previews");
    juce::File dirBranding = dirMedia.getChildFile("branding");
    juce::File dirCollections = destino.getChildFile("collections");
    juce::File dirCss = destino.getChildFile("css");
    juce::File dirJs = destino.getChildFile("js");

    dirThumbs.createDirectory();
    dirPreviews.createDirectory();
    dirBranding.createDirectory();
    dirCollections.createDirectory();
    dirCss.createDirectory();
    dirJs.createDirectory();

    // Process custom background image
    juce::String rootBgClass = "";
    juce::String rootBgStyle = "";
    juce::String colBgClass = "";
    juce::String colBgStyle = "";

    if (params.backgroundImageFile.existsAsFile()) {
        juce::String ext = params.backgroundImageFile.getFileExtension().toLowerCase();
        if (ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".webp") {
            juce::File destBg = dirBranding.getChildFile("background" + ext);
            params.backgroundImageFile.copyFileTo(destBg);

            rootBgClass = "has-custom-bg";
            rootBgStyle = "style=\"background-image: linear-gradient(rgba(9, 11, 14, 0.82), rgba(9, 11, 14, 0.82)), url('media/branding/background" + ext + "');\"";
            colBgClass = "has-custom-bg";
            colBgStyle = "style=\"background-image: linear-gradient(rgba(9, 11, 14, 0.82), rgba(9, 11, 14, 0.82)), url('../../media/branding/background" + ext + "');\"";
        }
    }

    // Process custom logo
    juce::String rootLogoHtml = "";
    juce::String colLogoHtml = "";
    juce::String splashLogoHtml = "";

    if (params.logoFile.existsAsFile()) {
        juce::String ext = params.logoFile.getFileExtension().toLowerCase();
        if (ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".webp" || ext == ".svg") {
            juce::File destLogo = dirBranding.getChildFile("logo" + ext);
            params.logoFile.copyFileTo(destLogo);

            rootLogoHtml = "<img class=\"catalog-logo\" src=\"media/branding/logo" + ext + "\" alt=\"Logo\">";
            colLogoHtml = "<img class=\"catalog-logo\" src=\"../../media/branding/logo" + ext + "\" alt=\"Logo\">";
            splashLogoHtml = "<img class=\"splash-logo\" src=\"media/branding/logo" + ext + "\" alt=\"Logo\">";
        }
    }

    // Write static CSS & JS templates
    dirCss.getChildFile("style.css").replaceWithText(kCssTemplate);
    dirJs.getChildFile("app.js").replaceWithText(kJsTemplate);

    // Calculate total assets across all valid collections
    std::vector<std::pair<ui::ProjetoAberto::ColecaoLink, std::vector<ui::ItemResumo>>> colecoesProcessadas;
    std::set<juce::String> usedSlugs;

    int totalGeralAssets = 0;
    for (const auto& c : colecoes) {
        if (!c.valido) continue;
        juce::File colDir(c.caminhoProjeto);
        auto todosItens = projeto.listarItensDaColecao(colDir);
        std::vector<ui::ItemResumo> itens;
        for (auto& it : todosItens) {
            if (it.marcadoPublicacao) {
                itens.push_back(std::move(it));
            }
        }
        totalGeralAssets += static_cast<int>(itens.size());
        colecoesProcessadas.push_back({c, std::move(itens)});
    }

    resultado.totalCollections = static_cast<int>(colecoesProcessadas.size());
    resultado.totalAssets = totalGeralAssets;

    auto manifestObj = std::make_unique<juce::DynamicObject>();
    manifestObj->setProperty("catalogName", juce::String(projeto.projeto().nome()));
    manifestObj->setProperty("generatedAt", juce::Time::getCurrentTime().toISO8601(true));
    manifestObj->setProperty("exportVersion", "1.0.0");
    manifestObj->setProperty("totalCollections", resultado.totalCollections);
    manifestObj->setProperty("totalAssets", resultado.totalAssets);

    auto collectionsArray = juce::Array<juce::var>();
    int feitoGlobal = 0;

    for (size_t colIdx = 0; colIdx < colecoesProcessadas.size(); ++colIdx) {
        const auto& [colLink, itens] = colecoesProcessadas[colIdx];
        juce::File colDir(colLink.caminhoProjeto);

        // Deterministic slug generation with collision handling
        juce::String baseSlug = gerarSlugColecao(colLink.nome);
        juce::String slug = baseSlug;
        int slugCounter = 2;
        while (usedSlugs.count(slug) > 0) {
            slug = baseSlug + "-" + juce::String(slugCounter++);
        }
        usedSlugs.insert(slug);

        juce::File colDestDir = dirCollections.getChildFile(slug);
        colDestDir.createDirectory();

        juce::String coverThumbRel;
        auto assetsArray = juce::Array<juce::var>();

        for (size_t i = 0; i < itens.size(); ++i) {
            if (aoProgredir && !aoProgredir(feitoGlobal, totalGeralAssets)) {
                resultado.cancelled = true;
                return resultado;
            }

            const auto& item = itens[i];
            juce::File masterFile = resolverArquivoMaster(colDir, item);

            juce::String thumbRel;
            juce::String previewAudioRel;
            juce::String previewVideoRel;

            // Handle offline master
            if (!masterFile.existsAsFile()) {
                SkippedAssetInfo skipped;
                skipped.itemId = item.id;
                skipped.title = juce::String(item.titulo);
                skipped.collectionName = colLink.nome;
                skipped.reason = "Master file offline/unreachable";
                resultado.skippedOfflineAssets.push_back(skipped);
            }

            // 1. High-resolution Web Thumbnail (800-1200px)
            juce::String thumbFilename = "thumb_" + slug + "_" + juce::String(item.id) + ".jpg";
            juce::File thumbTarget = dirThumbs.getChildFile(thumbFilename);

            if (gerarThumbnailWeb(masterFile, colDir, item, thumbTarget, params.thumbnailMaxPx)) {
                thumbRel = "../../media/thumbs/" + thumbFilename;
                resultado.thumbnailsGenerated++;
                if (coverThumbRel.isEmpty()) {
                    coverThumbRel = "media/thumbs/" + thumbFilename;
                }
            }

            // 2. Short 15s Previews
            std::string cat = item.tipoMidia;
            if (masterFile.existsAsFile()) {
                if (cat == "audio" || cat == "faixa" || cat == "musica" || cat == "stem") {
                    juce::String previewFilename = "preview_" + slug + "_" + juce::String(item.id) + ".m4a";
                    juce::File previewTarget = dirPreviews.getChildFile(previewFilename);
                    if (gerarPreviewAudio(masterFile, previewTarget, params.previewDurationSec)) {
                        previewAudioRel = "../../media/previews/" + previewFilename;
                        resultado.previewsGenerated++;
                    }
                } else if (cat == "video" || cat == "filme" || cat == "clipe") {
                    juce::String previewFilename = "preview_" + slug + "_" + juce::String(item.id) + ".mp4";
                    juce::File previewTarget = dirPreviews.getChildFile(previewFilename);
                    if (gerarPreviewVideo(masterFile, previewTarget, params.previewDurationSec)) {
                        previewVideoRel = "../../media/previews/" + previewFilename;
                        resultado.previewsGenerated++;
                    }
                }
            }

            // Build Asset JSON Object
            auto assetObj = std::make_unique<juce::DynamicObject>();
            assetObj->setProperty("id", juce::String(item.id));
            assetObj->setProperty("code", juce::String(item.codigoAcervo));
            assetObj->setProperty("title", juce::String(item.titulo));
            if (item.artistaLancamento) assetObj->setProperty("artist", juce::String(*item.artistaLancamento));
            if (item.ano) assetObj->setProperty("year", *item.ano);
            assetObj->setProperty("mediaType", juce::String(item.tipoMidia));
            if (item.contentType) assetObj->setProperty("contentType", juce::String(*item.contentType));
            if (item.collectionType) assetObj->setProperty("collectionType", juce::String(*item.collectionType));
            if (item.duracaoSegundos) {
                assetObj->setProperty("duration", *item.duracaoSegundos);
                assetObj->setProperty("durationFormatted", formatarDuracao(*item.duracaoSegundos));
            }
            if (thumbRel.isNotEmpty()) assetObj->setProperty("thumbnail", thumbRel);
            if (previewAudioRel.isNotEmpty()) assetObj->setProperty("previewAudio", previewAudioRel);
            if (previewVideoRel.isNotEmpty()) assetObj->setProperty("previewVideo", previewVideoRel);
            if (!item.isrc.empty()) assetObj->setProperty("isrc", juce::String(item.isrc));

            if (!item.tags.empty()) {
                juce::Array<juce::var> tagsArr;
                for (const auto& t : item.tags) tagsArr.add(juce::String(t));
                assetObj->setProperty("tags", tagsArr);
            }

            assetsArray.add(juce::var(assetObj.release()));
            feitoGlobal++;
        }

        // Handle collection pagination (Point 3)
        int totalItemsInCol = assetsArray.size();
        int pageSize = params.paginationLimit;
        int totalPages = (totalItemsInCol > 0) ? ((totalItemsInCol + pageSize - 1) / pageSize) : 1;

        auto colInfoObj = std::make_unique<juce::DynamicObject>();
        colInfoObj->setProperty("id", juce::String(colLink.id));
        colInfoObj->setProperty("name", colLink.nome);
        colInfoObj->setProperty("slug", slug);
        colInfoObj->setProperty("group", colLink.grupo);
        colInfoObj->setProperty("totalAssets", totalItemsInCol);
        colInfoObj->setProperty("totalPages", totalPages);
        colInfoObj->setProperty("pageSize", pageSize);

        auto colDataObj = std::make_unique<juce::DynamicObject>();
        colDataObj->setProperty("collection", juce::var(colInfoObj.release()));
        colDataObj->setProperty("totalPages", totalPages);
        colDataObj->setProperty("pageSize", pageSize);

        colDataObj->setProperty("assets", assetsArray);
        juce::String jsonStr = juce::JSON::toString(juce::var(colDataObj.release()), true);
        colDestDir.getChildFile("data.json").replaceWithText(jsonStr);

        if (totalItemsInCol > pageSize) {
            // Paginated chunks: data-1.json, data-2.json...
            for (int p = 0; p < totalPages; ++p) {
                int startIdx = p * pageSize;
                int count = std::min(pageSize, totalItemsInCol - startIdx);
                juce::Array<juce::var> pageAssets;
                for (int k = 0; k < count; ++k) {
                    pageAssets.add(assetsArray[startIdx + k]);
                }
                auto pageObj = std::make_unique<juce::DynamicObject>();
                pageObj->setProperty("page", p + 1);
                pageObj->setProperty("totalPages", totalPages);
                pageObj->setProperty("assets", pageAssets);
                juce::String pageStr = juce::JSON::toString(juce::var(pageObj.release()), true);
                colDestDir.getChildFile("data-" + juce::String(p + 1) + ".json").replaceWithText(pageStr);
            }
        }

        // Generate collection index.html
        juce::String colHtml = kCollectionHtmlTemplate;
        colHtml = colHtml.replace("%COLLECTION_SLUG%", slug);
        colHtml = colHtml.replace("%COLLECTION_NAME%", colLink.nome);
        colHtml = colHtml.replace("%COLLECTION_GROUP%", colLink.grupo.isNotEmpty() ? colLink.grupo : "Collection Archive");
        colHtml = colHtml.replace("%COLLECTION_TOTAL_ASSETS%", juce::String(totalItemsInCol));
        colHtml = colHtml.replace("%BODY_CUSTOM_BG_CLASS%", colBgClass);
        colHtml = colHtml.replace("%BODY_CUSTOM_BG_STYLE%", colBgStyle);
        colHtml = colHtml.replace("%CATALOG_LOGO_HTML%", colLogoHtml);
        colHtml = colHtml.replace("%COLLECTION_DATA_JSON%", jsonStr);
        colDestDir.getChildFile("index.html").replaceWithText(colHtml);

        // Add to Manifest
        auto colManifestObj = std::make_unique<juce::DynamicObject>();
        colManifestObj->setProperty("id", juce::String(colLink.id));
        colManifestObj->setProperty("name", colLink.nome);
        colManifestObj->setProperty("slug", slug);
        colManifestObj->setProperty("group", colLink.grupo);
        colManifestObj->setProperty("assetCount", totalItemsInCol);
        if (coverThumbRel.isNotEmpty()) colManifestObj->setProperty("coverThumb", coverThumbRel);
        collectionsArray.add(juce::var(colManifestObj.release()));
    }

    manifestObj->setProperty("collections", collectionsArray);
    juce::String manifestStr = juce::JSON::toString(juce::var(manifestObj.release()), true);
    destino.getChildFile("manifest.json").replaceWithText(manifestStr);

    // Write landing index.html with injected manifest data
    juce::String landingHtml = kLandingHtmlTemplate;
    landingHtml = landingHtml.replace("%CATALOG_NAME%", juce::String(projeto.projeto().nome()));
    landingHtml = landingHtml.replace("%TOTAL_COLLECTIONS%", juce::String(resultado.totalCollections));
    landingHtml = landingHtml.replace("%TOTAL_ASSETS%", juce::String(resultado.totalAssets));
    landingHtml = landingHtml.replace("%BODY_CUSTOM_BG_CLASS%", rootBgClass);
    landingHtml = landingHtml.replace("%BODY_CUSTOM_BG_STYLE%", rootBgStyle);
    landingHtml = landingHtml.replace("%SPLASH_LOGO_HTML%", splashLogoHtml);
    landingHtml = landingHtml.replace("%CATALOG_LOGO_HTML%", rootLogoHtml);
    landingHtml = landingHtml.replace("%MANIFEST_JSON%", manifestStr);
    destino.getChildFile("index.html").replaceWithText(landingHtml);

    if (aoProgredir) aoProgredir(totalGeralAssets, totalGeralAssets);
    return resultado;
}

} // namespace matriz::catalogo
