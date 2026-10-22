#include "WhatsappIngestao.h"

#include <algorithm>
#include <set>

#include "../I18n/Strings.h"
#include "../Ingest/Checksum.h"
#include "../Ingest/IngestArquivo.h"
#include "../Ingest/Miniaturas.h"
#include "../Ingest/ProcessoExterno.h"
#include "../Model/NomesSeguros.h"
#include "../Model/ProjectLog.h"
#include "../Ui/LogosTipoArquivo.h"
#include "../Ui/OriginalSourceMedium.h"

namespace matriz::whatsapp {

using matriz::db::Value;

namespace {

constexpr juce::int64 kTetoDoTxt = 768LL * 1024 * 1024;  // um .txt maior que isto não é uma conversa razoável

bool entradaIgnorada(const juce::String& nomeDaEntrada) {
    const juce::String n = nomeDaEntrada.replaceCharacter('\\', '/');
    if (n.endsWith("/")) return true;  // pasta
    if (n.startsWith("__MACOSX/") || n.contains("/__MACOSX/")) return true;
    const juce::String base = n.fromLastOccurrenceOf("/", false, false);
    return base.isEmpty() || base.startsWith(".") || base.equalsIgnoreCase("Thumbs.db");
}

juce::String textoDoBloco(const juce::MemoryBlock& b, int max = -1) {
    const int n = max >= 0 ? juce::jmin(max, static_cast<int>(b.getSize())) : static_cast<int>(b.getSize());
    return juce::String::fromUTF8(static_cast<const char*>(b.getData()), n);
}

std::unique_ptr<ConversaDetectada> montar(OrigemConversa tipo, const juce::File& raiz, ConversaParseada parseada, const juce::String& nomeDoTxt) {
    auto c = std::make_unique<ConversaDetectada>();
    c->tipo = tipo;
    c->zip = raiz;
    c->conversa = std::move(parseada);
    c->dataExportacao = raiz.getLastModificationTime();
    juce::String contato = nomeDoContatoNoArquivo(nomeDoTxt);
    if (contato.isEmpty()) {
        const juce::String base = raiz.getFileNameWithoutExtension();
        contato = nomeDoContatoNoArquivo(base + ".txt");
        if (contato.isEmpty()) contato = base;
    }
    if (contato.isEmpty()) contato = c->conversa.autores.isEmpty() ? juce::String("WhatsApp") : c->conversa.autores[0];
    c->contato = contato;
    return c;
}

// Lê o .txt do disco (limite de tamanho) e confere a assinatura. `forcar` aceita sem conferir.
std::optional<ConversaParseada> lerTxtDoDisco(const juce::File& txt, bool forcar) {
    if (!txt.existsAsFile() || txt.getSize() <= 0 || txt.getSize() > kTetoDoTxt) return std::nullopt;
    juce::MemoryBlock b;
    {
        juce::FileInputStream in(txt);
        if (in.failedToOpen()) return std::nullopt;
        in.readIntoMemoryBlock(b, -1);
    }
    if (!forcar && !primeirasLinhasSaoDeWhatsapp(textoDoBloco(b, 16384))) return std::nullopt;
    auto p = lerConversa(textoDoBloco(b));
    if (!forcar && !p.valida()) return std::nullopt;
    return p;
}

std::vector<juce::File> arquivosDaPasta(const juce::File& pasta) {
    std::vector<juce::File> out;
    for (const auto& f : pasta.findChildFiles(juce::File::findFiles, true)) {
        const auto rel = f.getRelativePathFrom(pasta);
        if (rel.contains("__MACOSX") || f.getFileName().startsWith(".") || f.getFileName().equalsIgnoreCase("Thumbs.db")) continue;
        out.push_back(f);
    }
    return out;
}

} // namespace

// ── Detecção ─────────────────────────────────────────────────────────────────────────────────

ConversaPtr detectarConversa(const juce::File& entrada, const std::atomic<bool>* cancelar, bool forcar) {
    try {
        // pasta descompactada: o .txt da exportação dentro dela
        if (entrada.isDirectory()) {
            for (const auto& txt : entrada.findChildFiles(juce::File::findFiles, false, "*.txt")) {
                if (cancelar != nullptr && cancelar->load()) return nullptr;
                if (txt.getFileName().startsWith(".")) continue;
                auto p = lerTxtDoDisco(txt, forcar);
                if (!p) continue;
                auto c = montar(OrigemConversa::Pasta, entrada, std::move(*p), txt.getFileName());
                c->txtNoDisco = txt;
                for (const auto& f : arquivosDaPasta(entrada)) c->abrangidos.push_back(f);
                return c;
            }
            return nullptr;
        }
        // .txt solto: as mídias citadas que estejam ao lado dele vêm junto
        if (entrada.existsAsFile() && entrada.hasFileExtension("txt")) {
            auto p = lerTxtDoDisco(entrada, forcar);
            if (!p) return nullptr;
            auto c = montar(OrigemConversa::Txt, entrada, std::move(*p), entrada.getFileName());
            c->txtNoDisco = entrada;
            c->abrangidos.push_back(entrada);
            std::set<juce::String> citados;
            for (const auto& m : c->conversa.mensagens)
                if (m.tipo == TipoMensagem::Anexo) citados.insert(chaveDeNome(m.anexo));
            for (const auto& irmao : entrada.getParentDirectory().findChildFiles(juce::File::findFiles, false))
                if (irmao != entrada && citados.count(chaveDeNome(irmao.getFileName())) != 0) c->abrangidos.push_back(irmao);
            return c;
        }
        if (!entrada.existsAsFile() || !entrada.hasFileExtension("zip") || entrada.getSize() < 22) return nullptr;

        juce::ZipFile zf(entrada);
        const int n = zf.getNumEntries();
        for (int i = 0; i < n; ++i) {
            if (cancelar != nullptr && cancelar->load()) return nullptr;
            const auto* e = zf.getEntry(i);
            if (e == nullptr || entradaIgnorada(e->filename)) continue;
            if (!e->filename.endsWithIgnoreCase(".txt")) continue;
            if (e->uncompressedSize <= 0 || e->uncompressedSize > kTetoDoTxt) continue;
            // 1ª checagem barata: a maioria das primeiras linhas tem o formato de mensagem?
            if (!forcar) {
                std::unique_ptr<juce::InputStream> s(zf.createStreamForEntry(i));
                if (s == nullptr) continue;
                juce::MemoryBlock inicio;
                s->readIntoMemoryBlock(inicio, 16384);
                if (!primeirasLinhasSaoDeWhatsapp(textoDoBloco(inicio))) continue;
            }
            std::unique_ptr<juce::InputStream> s(zf.createStreamForEntry(i));
            if (s == nullptr) continue;
            juce::MemoryBlock tudo;
            s->readIntoMemoryBlock(tudo, -1);
            auto parseada = lerConversa(textoDoBloco(tudo));
            if (!forcar && !parseada.valida()) continue;
            auto c = montar(OrigemConversa::Zip, entrada, std::move(parseada), e->filename.fromLastOccurrenceOf("/", false, false));
            c->entradaTxt = e->filename;
            c->abrangidos.push_back(entrada);
            return c;
        }
    } catch (const std::exception& e) {
        juce::Logger::writeToLog("WhatsApp: não foi possível inspecionar " + entrada.getFullPathName() + " — " + juce::String(e.what()));
    } catch (...) {
    }
    return nullptr;
}

std::vector<ConversaPtr> detectarConversasEm(const juce::Array<juce::File>& itens, const std::atomic<bool>* cancelar, bool forcar) {
    std::vector<ConversaPtr> out;
    std::set<juce::String> vistos;
    const auto tentar = [&](const juce::File& f) {
        if (vistos.count(f.getFullPathName()) != 0) return;
        if (auto c = detectarConversa(f, cancelar, forcar)) {
            vistos.insert(f.getFullPathName());
            for (const auto& a : c->abrangidos) vistos.insert(a.getFullPathName());
            out.push_back(std::move(c));
        }
    };
    for (const auto& f : itens) {
        if (cancelar != nullptr && cancelar->load()) break;
        if (f.isDirectory()) {
            tentar(f);  // a própria pasta é uma exportação descompactada?
            if (vistos.count(f.getFullPathName()) != 0) continue;
            for (const auto& z : f.findChildFiles(juce::File::findFiles, true, "*.zip")) {
                if (cancelar != nullptr && cancelar->load()) break;
                if (z.getFileName().startsWith(".")) continue;
                tentar(z);
            }
        } else if (f.existsAsFile() && (f.hasFileExtension("zip") || f.hasFileExtension("txt"))) {
            tentar(f);
        }
    }
    return out;
}

// ── Nomes ────────────────────────────────────────────────────────────────────────────────────

juce::String pastaRelativaDaConversa(const juce::String& contato) {
    return juce::String(kPastaRaizWhatsapp) + "/" +
           matriz::nomes_seguros::sanitizarComponente(contato.trim().isEmpty() ? juce::String("WhatsApp") : contato.trim());
}

juce::String tituloDaConversa(const juce::String& contato, bool pt) { return (pt ? "Conversa com " : "Chat with ") + contato; }  // i18n-ok: idioma explícito gravado no banco

juce::String descricaoDaConversa(const ConversaDetectada& c, bool pt) {
    juce::String d = (pt ? "Conversa do WhatsApp com " : "WhatsApp chat with ") + c.contato;  // i18n-ok: idioma explícito gravado no banco
    if (c.conversa.primeiraChaveDia > 0)
        d += juce::String::fromUTF8(" \xe2\x80\x94 ") + c.periodoInicio() + (pt ? " a " : " to ") + c.periodoFim();  // i18n-ok: idioma explícito gravado no banco
    return d;
}

// ── Áudio ────────────────────────────────────────────────────────────────────────────────────

AudioOriginal sondarAudio(const juce::File& arquivo) {
    AudioOriginal r;
    try {
        const std::string saida = matriz::ingest::capturarSaidaTexto(
            "ffprobe", argumentos({"-v", "error", "-select_streams", "a:0", "-show_entries", "stream=codec_name,sample_rate,channels:format=duration",
                                   "-of", "json", arquivo.getFullPathName()}),
            60000);
        const auto raiz = juce::JSON::parse(juce::String::fromUTF8(saida.c_str()));
        const auto streams = raiz["streams"];
        if (streams.isArray() && streams.size() > 0) {
            const auto& st = streams[0];
            r.codec = st["codec_name"].toString();
            r.taxa = st["sample_rate"].toString().getIntValue();
            r.canais = static_cast<int>(st["channels"]);
        }
        r.duracao = raiz["format"]["duration"].toString().getDoubleValue();
        r.ok = r.taxa > 0 && r.canais > 0;
    } catch (const std::exception& e) {
        juce::Logger::writeToLog("WhatsApp: ffprobe falhou em " + arquivo.getFileName() + " — " + juce::String(e.what()));
    }
    return r;
}

bool converterParaOgg(const juce::File& origem, const juce::File& destino, const TagsAudio& tags) {
    // Ogg Vorbis (qualidade ~5, mono): o leitor de áudio do JUCE toca nos 3 sistemas (AAC/M4A não toca no Linux).
    const auto tentar = [&](const juce::StringArray& codec) {
        try {
            destino.deleteFile();
            juce::StringArray args = argumentos({"-nostdin", "-y", "-v", "error", "-i", origem.getFullPathName(), "-vn", "-map", "0:a:0"});
            args.addArray(codec);
            args.addArray(argumentos({"-map_metadata", "-1"}));
            const auto meta = [&](const char* chave, const juce::String& valor) {
                if (valor.isEmpty()) return;
                args.add("-metadata");
                args.add(juce::String(chave) + "=" + valor);
            };
            meta("artist", tags.artist);
            meta("date", tags.date);
            meta("title", tags.title);
            meta("album", tags.album);
            meta("comment", tags.comment);
            args.add(destino.getFullPathName());
            matriz::ingest::rodarEsperandoSucesso("ffmpeg", args, 120000);
            return destino.existsAsFile() && destino.getSize() > 0;
        } catch (const std::exception& e) {
            juce::Logger::writeToLog("WhatsApp: conversão para OGG falhou em " + origem.getFileName() + " — " + juce::String(e.what()));
            destino.deleteFile();
            return false;
        }
    };
    if (tentar(argumentos({"-c:a", "libvorbis", "-q:a", "5", "-ac", "1"}))) return true;
    // ffmpeg sem libvorbis: o codificador Vorbis nativo (experimental) faz o mesmo serviço
    if (tentar(argumentos({"-c:a", "vorbis", "-strict", "-2", "-b:a", "96k", "-ac", "1"}))) return true;
    return tentar(argumentos({"-c:a", "vorbis", "-strict", "-2", "-b:a", "96k"}));
}

// ── Ingestão ─────────────────────────────────────────────────────────────────────────────────

namespace {

enum class Papel { Midia, OpusOriginal };

struct ArquivoDaConversa {
    juce::File arquivo;           // o que é ingerido (m4a convertido, ou o original)
    Papel papel = Papel::Midia;
    int indiceMensagem = -1;      // -1 = não citado no .txt
    juce::String nomeOriginal;    // como está no .txt (ou o nome do arquivo)
    juce::String nomeGravado;
    juce::String subpasta;
    bool convertido = false;
    AudioOriginal original;
};

juce::File nomeUnicoEm(const juce::File& pasta, const juce::String& nome) {
    juce::File f = pasta.getChildFile(nome);
    const int ponto = nome.lastIndexOfChar('.');
    const juce::String base = ponto > 0 ? nome.substring(0, ponto) : nome;
    const juce::String ext = ponto > 0 ? nome.substring(ponto) : juce::String();
    int n = 2;
    while (f.exists()) f = pasta.getChildFile(base + " (" + juce::String(n++) + ")" + ext);
    return f;
}

bool temColuna(matriz::db::Database& db, const std::string& tabela, const std::string& coluna) {
    auto st = db.prepare("SELECT COUNT(*) FROM pragma_table_info(?) WHERE name = ?");
    st.bind(1, Value::of(tabela));
    st.bind(2, Value::of(coluna));
    return st.step() && st.columnInt(0) > 0;
}

void mesclarOriginalNoArquivo(matriz::db::Database& db, const std::string& itemId, const ArquivoDaConversa& a) {
    auto st = db.prepare("SELECT id, caracteristicas_tecnicas_json FROM arquivo WHERE item_id = ? AND eh_master = 1 LIMIT 1");
    st.bind(1, Value::of(itemId));
    if (!st.step()) return;
    const std::string arquivoId = st.columnText(0);
    juce::var json = juce::JSON::parse(juce::String::fromUTF8(st.columnText(1).c_str()));
    auto* obj = json.getDynamicObject();
    juce::var novo;
    if (obj == nullptr) {
        novo = juce::var(new juce::DynamicObject());
        obj = novo.getDynamicObject();
    } else {
        novo = json;
    }
    auto* orig = new juce::DynamicObject();
    orig->setProperty("nomeOriginal", a.nomeOriginal);
    orig->setProperty("codec", a.original.codec);
    orig->setProperty("sampleRate", a.original.taxa);
    orig->setProperty("canais", a.original.canais);
    orig->setProperty("duracaoSegundos", a.original.duracao);
    obj->setProperty("whatsappOriginal", juce::var(orig));
    db.run("UPDATE arquivo SET caracteristicas_tecnicas_json = ? WHERE id = ?",
           {Value::of(juce::JSON::toString(novo, true).toStdString()), Value::of(arquivoId)});
}

// A miniatura de toda conversa é a logo do WhatsApp (assets/whatsapp.jpg), nunca uma das mídias.
void colocarLogoComoMiniatura(matriz::model::Project& staging, const std::string& itemId, const juce::File& tmp) {
    try {
        const auto logo = matriz::ui::miniaturaConversaWhatsapp(1024);
        if (!logo.isValid()) return;
        const juce::File arq = tmp.getChildFile("whatsapp_logo.png");
        arq.getParentDirectory().createDirectory();
        {
            juce::FileOutputStream out(arq);
            if (out.failedToOpen()) return;
            out.setPosition(0);
            out.truncate();
            juce::PNGImageFormat().writeImageToStream(logo, out);
        }
        auto st = staging.registro().prepare("SELECT id FROM arquivo WHERE item_id = ? AND eh_master = 1 LIMIT 1");
        st.bind(1, Value::of(itemId));
        if (!st.step()) return;
        const std::string arquivoId = st.columnText(0);
        try {
            staging.indice().run("DELETE FROM miniatura WHERE item_id = ?", {Value::of(itemId)});
        } catch (...) {
        }
        matriz::ingest::gerarEGravarMiniaturaPrincipal(staging.indice(), staging.pasta(), itemId, arquivoId, arq,
                                                       matriz::ingest::CategoriaMidia::Imagem, std::nullopt);
    } catch (const std::exception& e) {
        juce::Logger::writeToLog("WhatsApp: miniatura da conversa — " + juce::String(e.what()));
    }
}

} // namespace

ResultadoConversa ingerirConversa(matriz::model::Project& staging, const ConversaDetectada& c, const OpcoesConversaIngestao& opcoes) {
    ResultadoConversa res;
    const auto cancelado = [&] { return opcoes.cancelar != nullptr && opcoes.cancelar->load(); };
    auto& db = staging.registro();
    res.pastaRelativa = pastaRelativaDaConversa(c.contato);

    // tags automáticas: whatsapp e celular (sem diferenciar maiúsculas)
    auto sobre = opcoes.sobre;
    for (const char* t : {kTagWhatsapp, kTagCelular}) {
        const bool tem = std::any_of(sobre.tags.begin(), sobre.tags.end(),
                                     [&](const std::string& x) { return juce::String(x).equalsIgnoreCase(t); });
        if (!tem) sobre.tags.push_back(t);
    }
    if (sobre.collectionType.empty()) sobre.collectionType = kContentConversa;

    // importar de novo a mesma conversa (mesmo conteúdo) não duplica: usa a detecção de duplicatas que já existe
    const juce::File arquivoCapa = c.tipo == OrigemConversa::Zip ? c.zip : c.txtNoDisco;
    try {
        const auto sha = matriz::ingest::calcularChecksums(arquivoCapa).sha256;
        if (!sha.empty() && matriz::ingest::buscarAssetPorChecksum(db, sha, arquivoCapa.getSize())) {
            res.duplicada = true;
            res.ingestao.falhas.push_back({arquivoCapa.getFullPathName(), "This chat is already in this backup (same content), so it was not added again"});
            return res;
        }
    } catch (const std::exception& e) {
        juce::Logger::writeToLog("WhatsApp: checagem de duplicata — " + juce::String(e.what()));
    }

    // As mídias extraídas (e os .m4a convertidos) ficam na pasta da sessão de staging até o backup terminar: o MAIN é
    // copiado A PARTIR delas (é o "arquivo de origem" de cada item). A pasta some junto com a sessão de staging.
    const juce::File tmp = staging.pasta().getChildFile("extraidos_whatsapp").getChildFile(matriz::model::novoUuid());
    tmp.createDirectory();
    struct Limpeza {
        juce::File f;
        bool manter = false;
        ~Limpeza() { if (!manter) f.deleteRecursively(); }
    } limpeza{tmp};

    // 1) o que está disponível para esta conversa: extraído do zip, ou os arquivos que já estão no disco ──────
    std::map<juce::String, juce::File> disponiveis;  // chaveDeNome -> arquivo
    std::vector<juce::String> ordem;                 // na ordem de aparição
    juce::File txtParaIngerir;
    if (c.tipo == OrigemConversa::Zip) {
        juce::ZipFile zf(c.zip);
        const int n = zf.getNumEntries();
        for (int i = 0; i < n; ++i) {
            if (cancelado()) { res.ingestao.cancelado = true; return res; }
            const auto* e = zf.getEntry(i);
            if (e == nullptr || entradaIgnorada(e->filename)) continue;
            const juce::String base = e->filename.replaceCharacter('\\', '/').fromLastOccurrenceOf("/", false, false);
            std::unique_ptr<juce::InputStream> in(zf.createStreamForEntry(i));
            if (in == nullptr) continue;
            const juce::File dest = nomeUnicoEm(tmp, base);
            {
                juce::FileOutputStream out(dest);
                if (!out.openedOk()) {
                    res.ingestao.falhas.push_back({dest.getFullPathName(), "Could not extract the file from the zip"});
                    continue;
                }
                out.writeFromInputStream(*in, -1);
            }
            if (e->filename == c.entradaTxt) {
                txtParaIngerir = dest;  // o chat vai como item próprio (conversa.txt), além de ficar dentro do zip
                continue;
            }
            const auto chave = chaveDeNome(base);
            if (disponiveis.emplace(chave, dest).second) ordem.push_back(chave);
            if (opcoes.aoProgredir) opcoes.aoProgredir(i + 1, juce::jmax(1, n) * 2);
        }
    } else {
        std::vector<juce::File> candidatos;
        if (c.tipo == OrigemConversa::Pasta) {
            candidatos = arquivosDaPasta(c.zip);
        } else {
            for (const auto& a : c.abrangidos)
                if (a != c.txtNoDisco) candidatos.push_back(a);
        }
        for (const auto& f : candidatos) {
            if (f == c.txtNoDisco) continue;
            const auto chave = chaveDeNome(f.getFileName());
            if (disponiveis.emplace(chave, f).second) ordem.push_back(chave);
        }
    }

    // 2) liga cada anexo do .txt ao arquivo, converte os áudios (.opus etc. → Ogg Vorbis ~q5 mono) ───────────────
    std::vector<ArquivoDaConversa> lista;
    std::set<juce::String> usados;
    const auto album = (opcoes.portugues ? "Conversa do WhatsApp com " : "WhatsApp chat with ") + c.contato;
    const auto& msgs = c.conversa.mensagens;
    const bool guardarOpusOriginal = c.tipo != OrigemConversa::Zip;  // no zip o .opus original já está dentro do zip
    const auto adicionar = [&](const juce::File& f, int indiceMsg, const juce::String& nomeOriginal) {
        ArquivoDaConversa a;
        a.arquivo = f;
        a.papel = Papel::Midia;
        a.indiceMensagem = indiceMsg;
        a.nomeOriginal = nomeOriginal;
        a.nomeGravado = f.getFileName();
        a.subpasta = subpastaDeMes(nomeOriginal);
        const Mensagem* m = indiceMsg >= 0 ? &msgs[static_cast<size_t>(indiceMsg)] : nullptr;
        ArquivoDaConversa opusOriginal;
        bool temOpusOriginal = false;
        if (ehAudioParaConverter(f.getFileName())) {
            a.original = sondarAudio(f);
            TagsAudio tags;
            if (m != nullptr) {
                tags.artist = m->autor;
                tags.date = m->dataHoraTexto();
                tags.comment = m->legenda;
            }
            tags.title = nomeOriginal;
            tags.album = album;
            const juce::File ogg = nomeUnicoEm(tmp, f.getFileNameWithoutExtension() + ".ogg");
            if (cancelado()) return;
            if (converterParaOgg(f, ogg, tags)) {
                if (m != nullptr) {  // mantém a data: a do arquivo vira a da mensagem
                    const juce::Time quando(m->ano, m->mes - 1, m->dia, m->hora, m->minuto, m->segundo, 0, true);
                    ogg.setLastModificationTime(quando);
                }
                a.arquivo = ogg;
                a.nomeGravado = ogg.getFileName();
                a.convertido = true;
                ++res.audiosConvertidos;
                if (guardarOpusOriginal) {
                    opusOriginal.arquivo = f;
                    opusOriginal.papel = Papel::OpusOriginal;
                    opusOriginal.indiceMensagem = indiceMsg;
                    opusOriginal.nomeOriginal = nomeOriginal;
                    opusOriginal.nomeGravado = f.getFileName();
                    opusOriginal.subpasta = a.subpasta;
                    temOpusOriginal = true;
                }
            } else {
                ++res.audiosNaoConvertidos;  // fica o original
            }
        }
        lista.push_back(std::move(a));
        if (temOpusOriginal) lista.push_back(std::move(opusOriginal));
    };
    for (size_t i = 0; i < msgs.size(); ++i) {
        if (cancelado()) { res.ingestao.cancelado = true; return res; }
        if (msgs[i].tipo != TipoMensagem::Anexo) continue;
        const auto chave = chaveDeNome(msgs[i].anexo);
        auto it = disponiveis.find(chave);
        if (it == disponiveis.end() || !usados.insert(chave).second) continue;
        adicionar(it->second, static_cast<int>(i), msgs[i].anexo.trim());
    }
    for (const auto& chave : ordem) {  // arquivos que o .txt não cita
        if (cancelado()) { res.ingestao.cancelado = true; return res; }
        if (usados.count(chave) != 0) continue;
        adicionar(disponiveis[chave], -1, disponiveis[chave].getFileName());
    }

    // 3) ingere: a capa (zip, ou o conversa.txt), o conversa.txt e cada mídia, tudo como itens normais ──────────
    juce::Array<juce::File> aIngerir;
    std::map<juce::String, size_t> indicePorCaminho;
    aIngerir.add(arquivoCapa);
    if (c.tipo == OrigemConversa::Zip && txtParaIngerir.existsAsFile()) aIngerir.add(txtParaIngerir);
    for (size_t i = 0; i < lista.size(); ++i) {
        indicePorCaminho[lista[i].arquivo.getFullPathName()] = i;
        aIngerir.add(lista[i].arquivo);
    }

    db.execScript("CREATE TABLE IF NOT EXISTS collection_person (id TEXT PRIMARY KEY, nome TEXT NOT NULL UNIQUE, criado_em TEXT NOT NULL);");

    matriz::ingest::SobreMaterial sobreCapa = sobre;
    sobreCapa.pessoas.clear();
    for (const auto& a : c.conversa.autores) sobreCapa.pessoas.push_back(a.toStdString());
    if (sobreCapa.descricao.empty()) sobreCapa.descricao = descricaoDaConversa(c, opcoes.portugues).toStdString();
    std::unique_ptr<matriz::ingest::AplicadorSobreMaterial> aplicadorCapa;
    std::map<std::string, std::unique_ptr<matriz::ingest::AplicadorSobreMaterial>> aplicadoresPorAutor;
    const auto aplicadorDoAutor = [&](const juce::String& autor) -> matriz::ingest::AplicadorSobreMaterial& {
        auto& p = aplicadoresPorAutor[autor.toLowerCase().toStdString()];
        if (p == nullptr) {
            matriz::ingest::SobreMaterial s = sobre;
            s.pessoas.clear();
            if (autor.isNotEmpty()) s.pessoas.push_back(autor.toStdString());
            s.descricao.clear();
            p = std::make_unique<matriz::ingest::AplicadorSobreMaterial>(db, std::move(s));
        }
        return *p;
    };

    const auto dia = [](long long chave) {
        return chave > 0 ? juce::String::formatted("%04d-%02d-%02d", static_cast<int>(chave / 10000), static_cast<int>((chave / 100) % 100), static_cast<int>(chave % 100)) : juce::String();
    };
    const juce::String periodoIni = dia(c.conversa.primeiraChaveDia), periodoFim = dia(c.conversa.ultimaChaveDia);
    const auto exp = c.dataExportacao;
    const juce::String dataExportacaoTxt = juce::String::formatted("%04d-%02d-%02d", exp.getYear(), exp.getMonth() + 1, exp.getDayOfMonth());
    const bool legadoEntradaTxt = temColuna(db, "conversa_whatsapp", "entrada_txt");

    std::string txtItemId;
    std::vector<std::string> membros;  // itens do grupo (sem a capa)
    int ordemMidia = 0;
    matriz::ingest::OpcoesIngestao oi;
    oi.cancelar = opcoes.cancelar;
    oi.aoProgredir = [&](int feitos, int total) {
        if (opcoes.aoProgredir) {
            const int base = juce::jmax(1, total);
            opcoes.aoProgredir(base + feitos, base * 2);
        }
    };
    const auto inserirLinhaDoGrupo = [&](const std::string& itemId, int ordem, const juce::String& nomeOriginal, const juce::String& nomeArquivo,
                                         const char* papel, const juce::String& subpasta, const juce::String& autor, const juce::String& dataHora,
                                         const juce::String& legenda) {
        db.run("INSERT OR REPLACE INTO conversa_midia (conversa_item_id, midia_item_id, ordem, nome_original, nome_arquivo, papel, subpasta, autor, "
               "data_hora, legenda) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
               {Value::of(res.conversaItemId), Value::of(itemId), Value::of(ordem), Value::of(nomeOriginal.toStdString()),
                Value::of(nomeArquivo.toStdString()), Value::of(std::string(papel)),
                subpasta.isEmpty() ? Value::null() : Value::of(subpasta.toStdString()),
                autor.isEmpty() ? Value::null() : Value::of(autor.toStdString()),
                dataHora.isEmpty() ? Value::null() : Value::of(dataHora.toStdString()),
                legenda.isEmpty() ? Value::null() : Value::of(legenda.toStdString())});
        if (itemId != res.conversaItemId) membros.push_back(itemId);
    };

    oi.aoArquivoIngerido = [&](const std::string& itemId, const juce::File& arquivo) {
        const auto agora = matriz::model::agoraIso8601();
        if (arquivo == arquivoCapa && res.conversaItemId.empty()) {
            res.conversaItemId = itemId;
            aplicadorCapa = std::make_unique<matriz::ingest::AplicadorSobreMaterial>(db, sobreCapa);
            aplicadorCapa->aplicar(itemId);
            db.run("UPDATE item SET titulo = ?, ano = ?, atualizado_em = ? WHERE id = ?",
                   {Value::of(tituloDaConversa(c.contato, opcoes.portugues).toStdString()), Value::of(dataExportacaoTxt.toStdString()),
                    Value::of(agora), Value::of(itemId)});
            juce::String autoresJson = "[";
            for (int i = 0; i < c.conversa.autores.size(); ++i)
                autoresJson += (i ? "," : "") + juce::JSON::toString(juce::var(c.conversa.autores[i]), true);
            autoresJson += "]";
            const bool capaEhTxt = c.tipo != OrigemConversa::Zip;
            if (capaEhTxt) txtItemId = itemId;
            std::string sql = "INSERT OR REPLACE INTO conversa_whatsapp (item_id, contato, txt_item_id, voce, autores_json, periodo_inicio, periodo_fim, "
                              "n_mensagens, origem, idioma, data_exportacao, pasta_relativa, criado_em";
            std::string vals = "?, ?, ?, NULL, ?, ?, ?, ?, ?, ?, ?, ?, ?";
            std::vector<Value> params = {Value::of(itemId), Value::of(c.contato.toStdString()),
                                         capaEhTxt ? Value::of(itemId) : Value::null(), Value::of(autoresJson.toStdString()),
                                         Value::of(periodoIni.toStdString()), Value::of(periodoFim.toStdString()),
                                         Value::of(static_cast<int>(msgs.size())), Value::of(c.nomeDaOrigem().toStdString()),
                                         Value::of(c.conversa.idioma.toStdString()), Value::of(dataExportacaoTxt.toStdString()),
                                         Value::of(res.pastaRelativa.toStdString()), Value::of(agora)};
            if (legadoEntradaTxt) {
                sql += ", entrada_txt";
                vals += ", ?";
                params.push_back(Value::of(c.entradaTxt.isEmpty() ? c.txtNoDisco.getFileName().toStdString() : c.entradaTxt.toStdString()));
            }
            db.run(sql + ") VALUES (" + vals + ")", params);
            if (capaEhTxt) inserirLinhaDoGrupo(itemId, 0, arquivo.getFileName(), arquivo.getFileName(), "txt", {}, {}, {}, {});
            return;
        }
        if (c.tipo == OrigemConversa::Zip && arquivo == txtParaIngerir && txtItemId.empty() && !res.conversaItemId.empty()) {
            txtItemId = itemId;
            db.run("UPDATE conversa_whatsapp SET txt_item_id = ? WHERE item_id = ?", {Value::of(itemId), Value::of(res.conversaItemId)});
            aplicadorDoAutor({}).aplicar(itemId);
            inserirLinhaDoGrupo(itemId, 0, arquivo.getFileName(), arquivo.getFileName(), "txt", {}, {}, {}, {});
            return;
        }
        const auto it = indicePorCaminho.find(arquivo.getFullPathName());
        if (it == indicePorCaminho.end() || res.conversaItemId.empty()) return;
        const auto& a = lista[it->second];
        const Mensagem* m = a.indiceMensagem >= 0 ? &msgs[static_cast<size_t>(a.indiceMensagem)] : nullptr;
        const juce::String autor = m != nullptr ? m->autor : juce::String();
        const juce::String legenda = m != nullptr ? m->legenda : juce::String();
        const std::string leg = legenda.toStdString();
        aplicadorDoAutor(autor).aplicar(itemId, a.papel == Papel::Midia && !leg.empty() ? &leg : nullptr);
        if (m != nullptr)
            db.run("UPDATE item SET ano = ?, atualizado_em = ? WHERE id = ?",
                   {Value::of(m->dataHoraTexto().substring(0, 16).toStdString()), Value::of(agora), Value::of(itemId)});
        if (a.papel == Papel::Midia) {
            if (a.convertido) mesclarOriginalNoArquivo(db, itemId, a);
            ++res.midias;
        }
        inserirLinhaDoGrupo(itemId, ++ordemMidia, a.nomeOriginal, a.nomeGravado, a.papel == Papel::Midia ? "midia" : "opus_original", a.subpasta, autor,
                            m != nullptr ? m->dataHoraTexto() : juce::String(), a.papel == Papel::Midia ? legenda : juce::String());
    };

    limpeza.manter = true;  // daqui em diante os itens apontam para estes arquivos
    res.ingestao = matriz::ingest::ingerirNoStaging(staging, aIngerir, oi);

    // 4) o grupo: capa + conversa.txt + mídias = UM item no grid, que se move/apaga junto (mecanismo de NEST) ─────────
    // Único agrupamento automático que existe: a conversa e as mídias dela viram UM item (a capa = a conversa), então no canvas
    // aparece só a conversa, nunca os arquivos soltos. Qualquer outro NEST é só por ação do usuário (MAKE BACKUP > NEST).
    if (!res.conversaItemId.empty() && !membros.empty()) {
        try {
            const std::string nestId = matriz::model::novoUuid();
            const std::string agora = matriz::model::agoraIso8601();
            // origem = "whatsapp": a conversa chega ao INTAKE (como a galeria do celular) e precisa ser aprovada antes de ir ao Grid.
            db.run("INSERT INTO nest (id, projeto_id, capa_item_id, criado_em, origem) VALUES (?, ?, ?, ?, 'whatsapp')",
                   {Value::of(nestId), Value::of(staging.projetoId()), Value::of(res.conversaItemId), Value::of(agora)});
            const auto juntar = [&](const std::string& id) {
                db.run("INSERT OR REPLACE INTO nest_item (item_id, nest_id, adicionado_em) VALUES (?, ?, ?)", {Value::of(id), Value::of(nestId), Value::of(agora)});
                db.run("UPDATE item SET em_quarentena = 1 WHERE id = ?", {Value::of(id)});
            };
            juntar(res.conversaItemId);
            for (const auto& id : membros) juntar(id);
        } catch (const std::exception& e) {
            juce::Logger::writeToLog("WhatsApp: não foi possível agrupar a conversa — " + juce::String(e.what()));
        }
    }
    // Tudo que entra pelo fluxo PHONE DUMP recebe Original Source Medium = Phone Dump (é o que o filtro PHONE DUMP lê).
    if (!res.conversaItemId.empty()) {
        try {
            std::vector<std::string> idsPhoneDump = membros;
            idsPhoneDump.push_back(res.conversaItemId);
            matriz::ui::gravarOrigemPhoneDump(db, idsPhoneDump);
        } catch (const std::exception& e) {
            juce::Logger::writeToLog("WhatsApp: não foi possível marcar o Original Source Medium como Phone Dump — " + juce::String(e.what()));
        }
    }
    if (!res.conversaItemId.empty()) colocarLogoComoMiniatura(staging, res.conversaItemId, tmp);

    try {
        // Registro no log do projeto: a pasta WhatsApp/ é a única exceção à regra "MAIN = cópia fiel da origem".
        matriz::model::ProjectLog log(staging.pasta());
        log.appendEntry("WhatsApp Chat Ingested",
                        {"Chat: " + c.contato,
                         "Entry: " + c.zip.getFileName() + " (" + (c.tipo == OrigemConversa::Zip ? "zip" : (c.tipo == OrigemConversa::Pasta ? "folder" : "txt")) + ")",
                         "Folder in the backup: " + res.pastaRelativa,
                         "Media: " + juce::String(res.midias) + " (voice notes converted to Ogg Vorbis: " + juce::String(res.audiosConvertidos) + ")",
                         "Note: this folder is the only exception to \"the backup is a faithful copy of the source\": the zip (when there is one) is the "
                         "faithful original; the other files in it are derived from it."});
    } catch (...) {
    }
    juce::Logger::writeToLog("WhatsApp: conversa \"" + c.contato + "\" ingerida — " + juce::String(res.midias) + " mídia(s), " +
                             juce::String(res.audiosConvertidos) + " áudio(s) convertido(s) para OGG; pasta no MAIN: " + res.pastaRelativa);
    return res;
}

// ── Leitura ──────────────────────────────────────────────────────────────────────────────────

bool itemEhConversa(matriz::db::Database& registro, const std::string& itemId) {
    try {
        auto st = registro.prepare("SELECT 1 FROM conversa_whatsapp WHERE item_id = ?");
        st.bind(1, Value::of(itemId));
        return st.step();
    } catch (...) {
        return false;
    }
}

std::string capaDaConversaDoItem(matriz::db::Database& registro, const std::string& itemId) {
    if (itemEhConversa(registro, itemId)) return itemId;
    try {
        auto st = registro.prepare("SELECT conversa_item_id FROM conversa_midia WHERE midia_item_id = ? LIMIT 1");
        st.bind(1, Value::of(itemId));
        if (st.step()) return st.columnText(0);
    } catch (...) {
    }
    return {};
}

std::optional<InfoConversa> lerInfoConversa(matriz::db::Database& registro, const std::string& itemId) {
    try {
        auto st = registro.prepare("SELECT contato, COALESCE(txt_item_id, ''), voce, COALESCE(autores_json, '[]'), COALESCE(periodo_inicio, ''), "
                                   "COALESCE(periodo_fim, ''), data_exportacao, pasta_relativa, COALESCE(n_mensagens, 0), COALESCE(origem, ''), "
                                   "COALESCE(idioma, '') FROM conversa_whatsapp WHERE item_id = ?");
        st.bind(1, Value::of(itemId));
        if (!st.step()) return std::nullopt;
        InfoConversa r;
        r.itemId = itemId;
        r.contato = juce::String::fromUTF8(st.columnText(0).c_str());
        r.txtItemId = st.columnText(1);
        r.voceDefinido = !st.columnIsNull(2);
        r.voce = st.columnIsNull(2) ? juce::String() : juce::String::fromUTF8(st.columnText(2).c_str());
        const auto autores = juce::JSON::parse(juce::String::fromUTF8(st.columnText(3).c_str()));
        if (autores.isArray())
            for (const auto& a : *autores.getArray()) r.autores.add(a.toString());
        r.periodoInicio = juce::String::fromUTF8(st.columnText(4).c_str());
        r.periodoFim = juce::String::fromUTF8(st.columnText(5).c_str());
        r.dataExportacao = juce::String::fromUTF8(st.columnText(6).c_str());
        r.pastaRelativa = juce::String::fromUTF8(st.columnText(7).c_str());
        r.nMensagens = st.columnInt(8);
        r.origem = juce::String::fromUTF8(st.columnText(9).c_str());
        r.idioma = juce::String::fromUTF8(st.columnText(10).c_str());

        auto sm = registro.prepare("SELECT midia_item_id, ordem, nome_original, nome_arquivo, COALESCE(papel, 'midia'), COALESCE(subpasta, ''), "
                                   "COALESCE(autor, ''), COALESCE(data_hora, ''), COALESCE(legenda, '') FROM conversa_midia "
                                   "WHERE conversa_item_id = ? ORDER BY ordem");
        sm.bind(1, Value::of(itemId));
        while (sm.step()) {
            LinhaMidia l;
            l.midiaItemId = sm.columnText(0);
            l.ordem = sm.columnInt(1);
            l.nomeOriginal = juce::String::fromUTF8(sm.columnText(2).c_str());
            l.nomeArquivo = juce::String::fromUTF8(sm.columnText(3).c_str());
            l.papel = juce::String::fromUTF8(sm.columnText(4).c_str());
            l.subpasta = juce::String::fromUTF8(sm.columnText(5).c_str());
            l.autor = juce::String::fromUTF8(sm.columnText(6).c_str());
            l.dataHora = juce::String::fromUTF8(sm.columnText(7).c_str());
            l.legenda = juce::String::fromUTF8(sm.columnText(8).c_str());
            r.midias.push_back(std::move(l));
        }
        return r;
    } catch (const std::exception& e) {
        juce::Logger::writeToLog("WhatsApp: leitura da conversa falhou — " + juce::String(e.what()));
        return std::nullopt;
    }
}

std::map<std::string, DestinoConversa> destinosDosItensDeConversa(matriz::db::Database& registro) {
    std::map<std::string, DestinoConversa> out;
    try {
        auto st = registro.prepare("SELECT item_id, pasta_relativa, COALESCE(txt_item_id, '') FROM conversa_whatsapp");
        while (st.step()) {
            DestinoConversa d;
            d.pasta = juce::String::fromUTF8(st.columnText(1).c_str());
            if (st.columnText(2) == st.columnText(0)) d.nome = kNomeConversaTxt;  // a capa é o próprio conversa.txt
            out[st.columnText(0)] = d;
        }
        auto sm = registro.prepare("SELECT m.midia_item_id, w.pasta_relativa, COALESCE(m.subpasta, ''), COALESCE(m.papel, 'midia') "
                                   "FROM conversa_midia m JOIN conversa_whatsapp w ON w.item_id = m.conversa_item_id");
        while (sm.step()) {
            DestinoConversa d;
            d.pasta = juce::String::fromUTF8(sm.columnText(1).c_str());
            const juce::String papel = juce::String::fromUTF8(sm.columnText(3).c_str());
            const juce::String sub = juce::String::fromUTF8(sm.columnText(2).c_str());
            if (papel == "txt") d.nome = kNomeConversaTxt;
            else if (sub.isNotEmpty()) d.pasta += "/" + sub;
            out.emplace(sm.columnText(0), d);
        }
    } catch (...) {
    }
    return out;
}

void gravarVoce(matriz::db::Database& registro, const std::string& itemId, const juce::String& voce) {
    registro.run("UPDATE conversa_whatsapp SET voce = ? WHERE item_id = ?", {Value::of(voce.toStdString()), Value::of(itemId)});
}

std::map<juce::String, LinhaMidia> mapaMidiasPorNomeOriginal(const InfoConversa& info) {
    std::map<juce::String, LinhaMidia> m;
    for (const auto& l : info.midias)
        if (l.papel == "midia") m.emplace(chaveDeNome(l.nomeOriginal), l);
    return m;
}

} // namespace matriz::whatsapp
