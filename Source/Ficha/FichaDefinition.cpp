#include "FichaDefinition.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>

namespace matriz::ficha {

namespace {

// --- Wrappers finos sobre yaml-cpp -----------------------------------------
// Isolam o resto do arquivo da API do yaml-cpp e preservam o comportamento
// "ausente vira vazio/false, nunca exceção" que o resto da validação espera.

bool has(const YAML::Node& parent, const char* key) {
    return parent.IsDefined() && parent.IsMap() && parent[key].IsDefined();
}

YAML::Node get(const YAML::Node& parent, const char* key) {
    if (!parent.IsDefined() || !parent.IsMap()) return YAML::Node();
    return parent[key];
}

std::string asString(const YAML::Node& n, const std::string& fallback = "") {
    if (!n.IsDefined() || !n.IsScalar()) return fallback;
    return n.as<std::string>();
}

bool asBool(const YAML::Node& n, bool fallback = false) {
    if (!n.IsDefined() || !n.IsScalar()) return fallback;
    try {
        return n.as<bool>();
    } catch (const YAML::BadConversion&) {
        return fallback;
    }
}

std::vector<std::string> asStringList(const YAML::Node& n) {
    std::vector<std::string> out;
    if (n.IsDefined() && n.IsSequence())
        for (const auto& item : n) out.push_back(asString(item));
    return out;
}

std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t");
    if (b == std::string::npos) return {};
    size_t e = s.find_last_not_of(" \t");
    return s.substr(b, e - b + 1);
}

std::vector<std::string> splitFlowList(const std::string& inner) {
    std::vector<std::string> out;
    std::istringstream ss(inner);
    std::string item;
    while (std::getline(ss, item, ','))
        out.push_back(trim(item));
    return out;
}

const std::set<std::string>& validacoesConhecidas() {
    static const std::set<std::string> s = {"isrc", "ean13", "soma_100"};
    return s;
}

const std::set<std::string>& efeitosConhecidos() {
    static const std::set<std::string> s = {"habilitar_transcricao"};
    return s;
}

VisivelSe parseVisivelSe(const std::string& rawExpr) {
    std::string expr = trim(rawExpr);

    auto posIn = expr.find(" in ");
    if (posIn != std::string::npos) {
        std::string campoId = trim(expr.substr(0, posIn));
        std::string rest = trim(expr.substr(posIn + 4));
        if (rest.size() < 2 || rest.front() != '[' || rest.back() != ']')
            throw FichaDefinitionError("visivel_se: lista malformada em \"" + rawExpr + "\"");
        VisivelSe v;
        v.campoId = campoId;
        v.op = VisivelSeOperador::In;
        v.valores = splitFlowList(rest.substr(1, rest.size() - 2));
        if (campoId.empty() || v.valores.empty())
            throw FichaDefinitionError("visivel_se malformado: \"" + rawExpr + "\"");
        return v;
    }

    auto posEq = expr.find("==");
    auto posNeq = expr.find("!=");
    if (posNeq != std::string::npos && (posEq == std::string::npos || posNeq < posEq)) {
        VisivelSe v;
        v.campoId = trim(expr.substr(0, posNeq));
        v.op = VisivelSeOperador::Diferente;
        v.valores = {trim(expr.substr(posNeq + 2))};
        if (v.campoId.empty() || v.valores.front().empty())
            throw FichaDefinitionError("visivel_se malformado: \"" + rawExpr + "\"");
        return v;
    }
    if (posEq != std::string::npos) {
        VisivelSe v;
        v.campoId = trim(expr.substr(0, posEq));
        v.op = VisivelSeOperador::Igual;
        v.valores = {trim(expr.substr(posEq + 2))};
        if (v.campoId.empty() || v.valores.front().empty())
            throw FichaDefinitionError("visivel_se malformado: \"" + rawExpr + "\"");
        return v;
    }

    throw FichaDefinitionError("visivel_se não reconhecido (esperava \"campo in [...]\", \"campo == valor\" ou \"campo != valor\"): \"" + rawExpr + "\"");
}

// Slug ASCII estável derivado de um rótulo em português (ex. "Estado físico"
// -> "estado_fisico"). Base da chave de tradução de grupo em i18n/en.yaml —
// precisa ser determinístico e não mudar se o rótulo em si não mudar.
std::string slugify(const std::string& texto) {
    static const std::vector<std::pair<std::string, std::string>> acentos = {
        {"á", "a"}, {"à", "a"}, {"ã", "a"}, {"â", "a"}, {"ä", "a"},
        {"Á", "a"}, {"À", "a"}, {"Ã", "a"}, {"Â", "a"}, {"Ä", "a"},
        {"é", "e"}, {"è", "e"}, {"ê", "e"}, {"ë", "e"},
        {"É", "e"}, {"È", "e"}, {"Ê", "e"}, {"Ë", "e"},
        {"í", "i"}, {"ì", "i"}, {"î", "i"}, {"ï", "i"},
        {"Í", "i"}, {"Ì", "i"}, {"Î", "i"}, {"Ï", "i"},
        {"ó", "o"}, {"ò", "o"}, {"õ", "o"}, {"ô", "o"}, {"ö", "o"},
        {"Ó", "o"}, {"Ò", "o"}, {"Õ", "o"}, {"Ô", "o"}, {"Ö", "o"},
        {"ú", "u"}, {"ù", "u"}, {"û", "u"}, {"ü", "u"},
        {"Ú", "u"}, {"Ù", "u"}, {"Û", "u"}, {"Ü", "u"},
        {"ç", "c"}, {"Ç", "c"}, {"ñ", "n"}, {"Ñ", "n"},
    };
    std::string s = texto;
    for (auto& [de, para] : acentos) {
        size_t pos = 0;
        while ((pos = s.find(de, pos)) != std::string::npos) {
            s.replace(pos, de.size(), para);
            pos += para.size();
        }
    }
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            out.push_back(c);
        } else if (c >= 'A' && c <= 'Z') {
            out.push_back(static_cast<char>(c - 'A' + 'a'));
        } else if (!out.empty() && out.back() != '_') {
            out.push_back('_');
        }
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out;
}

void checkIdsUnicos(const std::vector<Campo>& campos, const std::string& contexto) {
    std::set<std::string> vistos;
    for (auto& c : campos) {
        if (!vistos.insert(c.id).second)
            throw FichaDefinitionError("id de campo duplicado \"" + c.id + "\" em " + contexto);
    }
}

std::vector<Campo> parseCampos(const YAML::Node& camposNode, const std::string& contexto) {
    if (!camposNode.IsDefined() || !camposNode.IsSequence())
        throw FichaDefinitionError("\"campos\" deve ser uma sequência em " + contexto);

    std::vector<Campo> campos;
    for (const auto& item : camposNode) {
        if (!item.IsMap())
            throw FichaDefinitionError("cada campo deve ser um mapa em " + contexto);

        Campo c;
        c.id = asString(get(item, "id"));
        c.rotulo = asString(get(item, "rotulo"));
        std::string tipoStr = asString(get(item, "tipo"));

        if (c.id.empty())
            throw FichaDefinitionError("campo sem \"id\" em " + contexto);
        if (c.rotulo.empty())
            throw FichaDefinitionError("campo \"" + c.id + "\" sem \"rotulo\" em " + contexto);
        if (tipoStr.empty())
            throw FichaDefinitionError("campo \"" + c.id + "\" sem \"tipo\" em " + contexto);
        c.tipo = campoTipoFromString(tipoStr);

        c.obrigatorio = asBool(get(item, "obrigatorio"), false);

        if (has(item, "opcoes")) {
            if (!get(item, "opcoes").IsSequence())
                throw FichaDefinitionError("campo \"" + c.id + "\": \"opcoes\" deve ser uma lista");
            c.opcoes = asStringList(get(item, "opcoes"));
        }
        if (c.tipo == CampoTipo::Opcao && c.opcoes.empty())
            throw FichaDefinitionError("campo \"" + c.id + "\": tipo \"opcao\" requer \"opcoes\"");

        if (has(item, "colunas")) {
            if (!get(item, "colunas").IsSequence())
                throw FichaDefinitionError("campo \"" + c.id + "\": \"colunas\" deve ser uma lista");
            c.colunas = asStringList(get(item, "colunas"));
        }
        if (c.tipo == CampoTipo::Tabela && c.colunas.empty())
            throw FichaDefinitionError("campo \"" + c.id + "\": tipo \"tabela\" requer \"colunas\"");

        c.validacao = asString(get(item, "validacao"));
        if (!c.validacao.empty() && !isValidacaoConhecida(c.validacao))
            throw FichaDefinitionError("campo \"" + c.id + "\": validação desconhecida \"" + c.validacao + "\"");
        if (c.validacao == "soma_100" && c.tipo != CampoTipo::Tabela)
            throw FichaDefinitionError("campo \"" + c.id + "\": validação \"soma_100\" só se aplica a tipo \"tabela\"");

        std::string visivelSeStr = asString(get(item, "visivel_se"));
        if (!visivelSeStr.empty())
            c.visivelSe = parseVisivelSe(visivelSeStr);

        c.herdaDoProjeto = asBool(get(item, "herda_do_projeto"), false);
        c.preenchidoPor = asString(get(item, "preenchido_por"));
        c.sugeridoPor = asString(get(item, "sugerido_por"));

        int origens = (c.herdaDoProjeto ? 1 : 0) + (!c.preenchidoPor.empty() ? 1 : 0) + (!c.sugeridoPor.empty() ? 1 : 0);
        if (origens > 1)
            throw FichaDefinitionError("campo \"" + c.id + "\": herda_do_projeto, preenchido_por e sugerido_por são mutuamente exclusivos");

        if (has(item, "afeta")) {
            if (!get(item, "afeta").IsSequence())
                throw FichaDefinitionError("campo \"" + c.id + "\": \"afeta\" deve ser uma lista");
            c.afeta = asStringList(get(item, "afeta"));
            for (auto& efeito : c.afeta)
                if (!isEfeitoConhecido(efeito))
                    throw FichaDefinitionError("campo \"" + c.id + "\": efeito desconhecido em \"afeta\": \"" + efeito + "\"");
        }

        c.alertaSeTrue = asString(get(item, "alerta_se_true"));
        if (!c.alertaSeTrue.empty() && c.tipo != CampoTipo::Booleano)
            throw FichaDefinitionError("campo \"" + c.id + "\": \"alerta_se_true\" só se aplica a tipo \"booleano\"");

        c.gerarEmSequencia = asBool(get(item, "gerar_em_sequencia"), false);

        campos.push_back(std::move(c));
    }

    checkIdsUnicos(campos, contexto);
    return campos;
}

// Checa visivel_se contra o conjunto completo de campos do mesmo nível
// lógico. Grupos são só organização visual (docs/formato-ficha.md — "não têm
// efeito em validação ou armazenamento"), então esta checagem roda depois de
// juntar todos os grupos de um nível, nunca por grupo isolado.
void checkVisivelSeReferences(const std::vector<Campo>& campos, const std::string& contexto) {
    for (auto& c : campos) {
        if (!c.visivelSe) continue;
        bool encontrado = std::any_of(campos.begin(), campos.end(),
                                       [&](const Campo& outro) { return outro.id == c.visivelSe->campoId; });
        if (!encontrado)
            throw FichaDefinitionError("campo \"" + c.id + "\": visivel_se referencia campo inexistente \"" + c.visivelSe->campoId + "\" em " + contexto);
    }
}

} // namespace

CampoTipo campoTipoFromString(const std::string& s) {
    if (s == "texto") return CampoTipo::Texto;
    if (s == "numero") return CampoTipo::Numero;
    if (s == "inteiro") return CampoTipo::Inteiro;
    if (s == "data") return CampoTipo::Data;
    if (s == "booleano") return CampoTipo::Booleano;
    if (s == "opcao") return CampoTipo::Opcao;
    if (s == "opcao_livre") return CampoTipo::OpcaoLivre;
    if (s == "lista_pessoas") return CampoTipo::ListaPessoas;
    if (s == "tabela") return CampoTipo::Tabela;
    throw FichaDefinitionError("tipo de campo desconhecido: \"" + s + "\"");
}

std::string campoTipoToString(CampoTipo t) {
    switch (t) {
        case CampoTipo::Texto: return "texto";
        case CampoTipo::Numero: return "numero";
        case CampoTipo::Inteiro: return "inteiro";
        case CampoTipo::Data: return "data";
        case CampoTipo::Booleano: return "booleano";
        case CampoTipo::Opcao: return "opcao";
        case CampoTipo::OpcaoLivre: return "opcao_livre";
        case CampoTipo::ListaPessoas: return "lista_pessoas";
        case CampoTipo::Tabela: return "tabela";
    }
    return "desconhecido";
}

bool isValidacaoConhecida(const std::string& nome) { return validacoesConhecidas().count(nome) > 0; }
bool isEfeitoConhecido(const std::string& nome) { return efeitosConhecidos().count(nome) > 0; }

std::vector<const Campo*> FichaDefinition::todosCampos() const {
    std::vector<const Campo*> out;
    if (usaNiveis()) {
        for (auto& kv : camposPorNivel)
            for (auto& c : kv.second)
                out.push_back(&c);
    } else {
        for (auto& g : grupos)
            for (auto& c : g.campos)
                out.push_back(&c);
    }
    return out;
}

const std::vector<Campo>* FichaDefinition::camposDoNivel(const std::string& nivel) const {
    for (auto& kv : camposPorNivel)
        if (kv.first == nivel)
            return &kv.second;
    return nullptr;
}

FichaDefinition loadFromString(const std::string& yamlText, const std::string& origemParaErro) {
    YAML::Node root;
    try {
        root = YAML::Load(yamlText);
    } catch (const YAML::Exception& e) {
        throw FichaDefinitionError(origemParaErro + ": erro de YAML: " + e.what());
    }

    try {
        if (!root.IsMap())
            throw FichaDefinitionError("a definição de ficha deve ser um mapa no nível raiz");

        FichaDefinition def;
        def.tipo = asString(get(root, "tipo"));
        def.rotulo = asString(get(root, "rotulo"));
        if (def.tipo.empty())
            throw FichaDefinitionError("\"tipo\" é obrigatório no nível raiz");
        if (def.rotulo.empty())
            throw FichaDefinitionError("\"rotulo\" é obrigatório no nível raiz");

        if (has(root, "modos")) {
            YAML::Node modosNode = get(root, "modos");
            if (!modosNode.IsSequence())
                throw FichaDefinitionError("tipo \"" + def.tipo + "\": \"modos\" deve ser uma lista");
            def.modos = asStringList(modosNode);
            for (auto& m : def.modos)
                if (m != "archive" && m != "catalog")
                    throw FichaDefinitionError("tipo \"" + def.tipo + "\": modo desconhecido em \"modos\": \"" + m +
                                                "\" (esperado \"archive\" ou \"catalog\")");
        }

        if (has(root, "ordem")) {
            YAML::Node ordemNode = get(root, "ordem");
            if (!ordemNode.IsScalar())
                throw FichaDefinitionError("tipo \"" + def.tipo + "\": \"ordem\" deve ser um número inteiro");
            try {
                def.ordem = ordemNode.as<int>();
            } catch (const YAML::BadConversion&) {
                throw FichaDefinitionError("tipo \"" + def.tipo + "\": \"ordem\" deve ser um número inteiro");
            }
        }

        def.icone = asString(get(root, "icone"));
        if (has(root, "categoria")) def.categoria = asString(get(root, "categoria"));

        bool hasGrupos = has(root, "grupos");
        bool hasNiveis = has(root, "niveis");
        if (hasGrupos == hasNiveis)
            throw FichaDefinitionError("tipo \"" + def.tipo + "\": definição deve ter \"grupos\" OU \"niveis\", nunca os dois nem nenhum");

        if (hasNiveis) {
            YAML::Node niveisNode = get(root, "niveis");
            if (!niveisNode.IsSequence())
                throw FichaDefinitionError("tipo \"" + def.tipo + "\": \"niveis\" deve ser uma lista");
            def.niveis = asStringList(niveisNode);
            if (def.niveis.empty())
                throw FichaDefinitionError("tipo \"" + def.tipo + "\": \"niveis\" não pode ser vazio");
            if (def.niveis.size() > 2)
                throw FichaDefinitionError("tipo \"" + def.tipo + "\": este formato suporta no máximo dois níveis (raiz + um repetido)");

            for (auto& nivel : def.niveis) {
                YAML::Node bloco = get(root, nivel.c_str());
                if (!bloco.IsMap())
                    throw FichaDefinitionError("tipo \"" + def.tipo + "\": bloco do nível \"" + nivel + "\" ausente ou inválido");
                YAML::Node camposNode = get(bloco, "campos");
                std::string contexto = "nível \"" + nivel + "\" do tipo \"" + def.tipo + "\"";
                std::vector<Campo> campos = parseCampos(camposNode, contexto);
                checkVisivelSeReferences(campos, contexto);
                def.camposPorNivel.emplace_back(nivel, std::move(campos));
            }
        } else {
            YAML::Node gruposNode = get(root, "grupos");
            if (!gruposNode.IsSequence())
                throw FichaDefinitionError("tipo \"" + def.tipo + "\": \"grupos\" deve ser uma lista");
            if (gruposNode.size() == 0)
                throw FichaDefinitionError("tipo \"" + def.tipo + "\": \"grupos\" não pode ser vazio");

            std::vector<Campo> todosOsCamposDaRaiz; // para checar unicidade de id entre grupos
            for (const auto& g : gruposNode) {
                if (!g.IsMap())
                    throw FichaDefinitionError("tipo \"" + def.tipo + "\": cada grupo deve ser um mapa");
                Grupo grupo;
                grupo.rotulo = asString(get(g, "rotulo"));
                if (grupo.rotulo.empty())
                    throw FichaDefinitionError("tipo \"" + def.tipo + "\": grupo sem \"rotulo\"");
                grupo.chave = slugify(grupo.rotulo);
                if (grupo.chave.empty())
                    throw FichaDefinitionError("tipo \"" + def.tipo + "\": grupo \"" + grupo.rotulo +
                                                "\" gera uma chave de tradução vazia");
                grupo.campos = parseCampos(get(g, "campos"), "grupo \"" + grupo.rotulo + "\" do tipo \"" + def.tipo + "\"");
                for (auto& c : grupo.campos)
                    todosOsCamposDaRaiz.push_back(c);
                def.grupos.push_back(std::move(grupo));
            }
            checkIdsUnicos(todosOsCamposDaRaiz, "tipo \"" + def.tipo + "\" (ids devem ser únicos entre todos os grupos)");
            checkVisivelSeReferences(todosOsCamposDaRaiz, "tipo \"" + def.tipo + "\" (todos os grupos)");

            std::set<std::string> chavesDeGrupoVistas;
            for (auto& gr : def.grupos)
                if (!chavesDeGrupoVistas.insert(gr.chave).second)
                    throw FichaDefinitionError("tipo \"" + def.tipo + "\": dois grupos geram a mesma chave de tradução \"" +
                                                gr.chave + "\" (\"" + gr.rotulo + "\")");
        }

        if (has(root, "arquivos_esperados")) {
            YAML::Node arqNode = get(root, "arquivos_esperados");
            if (!arqNode.IsSequence())
                throw FichaDefinitionError("tipo \"" + def.tipo + "\": \"arquivos_esperados\" deve ser uma lista");
            for (const auto& item : arqNode) {
                if (!item.IsMap())
                    throw FichaDefinitionError("tipo \"" + def.tipo + "\": cada entrada de \"arquivos_esperados\" deve ser um mapa");
                ArquivoEsperado ae;
                ae.papel = asString(get(item, "papel"));
                if (ae.papel.empty())
                    throw FichaDefinitionError("tipo \"" + def.tipo + "\": entrada de \"arquivos_esperados\" sem \"papel\"");
                ae.obrigatorio = asBool(get(item, "obrigatorio"), false);
                ae.por = asString(get(item, "por"));
                if (!ae.por.empty() && std::find(def.niveis.begin(), def.niveis.end(), ae.por) == def.niveis.end())
                    throw FichaDefinitionError("tipo \"" + def.tipo + "\": arquivos_esperados[" + ae.papel + "].por = \"" + ae.por + "\" não é um nível declarado em \"niveis\"");
                ae.minimo = asString(get(item, "minimo"));
                def.arquivosEsperados.push_back(std::move(ae));
            }
        }

        return def;
    } catch (const FichaDefinitionError& e) {
        throw FichaDefinitionError(origemParaErro + ": " + e.what());
    }
}

FichaDefinition loadFromFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw FichaDefinitionError("não foi possível abrir o arquivo: " + path);
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return loadFromString(buffer.str(), path);
}

} // namespace matriz::ficha
