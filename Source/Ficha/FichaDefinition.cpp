#include "FichaDefinition.h"

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>

namespace matriz::ficha {

using matriz::ficha_yaml::Node;
using matriz::ficha_yaml::NodeType;

namespace {

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

void checkIdsUnicos(const std::vector<Campo>& campos, const std::string& contexto) {
    std::set<std::string> vistos;
    for (auto& c : campos) {
        if (!vistos.insert(c.id).second)
            throw FichaDefinitionError("id de campo duplicado \"" + c.id + "\" em " + contexto);
    }
}

std::vector<Campo> parseCampos(const Node& camposNode, const std::string& contexto) {
    if (!camposNode.isSequence())
        throw FichaDefinitionError("\"campos\" deve ser uma sequência em " + contexto);

    std::vector<Campo> campos;
    for (auto& item : camposNode.sequence) {
        if (!item.isMap())
            throw FichaDefinitionError("cada campo deve ser um mapa em " + contexto);

        Campo c;
        c.id = item.get("id").asString();
        c.rotulo = item.get("rotulo").asString();
        std::string tipoStr = item.get("tipo").asString();

        if (c.id.empty())
            throw FichaDefinitionError("campo sem \"id\" em " + contexto);
        if (c.rotulo.empty())
            throw FichaDefinitionError("campo \"" + c.id + "\" sem \"rotulo\" em " + contexto);
        if (tipoStr.empty())
            throw FichaDefinitionError("campo \"" + c.id + "\" sem \"tipo\" em " + contexto);
        c.tipo = campoTipoFromString(tipoStr);

        c.obrigatorio = item.get("obrigatorio").asBool(false);

        if (item.has("opcoes")) {
            if (!item.get("opcoes").isSequence())
                throw FichaDefinitionError("campo \"" + c.id + "\": \"opcoes\" deve ser uma lista");
            c.opcoes = item.get("opcoes").asStringList();
        }
        if (c.tipo == CampoTipo::Opcao && c.opcoes.empty())
            throw FichaDefinitionError("campo \"" + c.id + "\": tipo \"opcao\" requer \"opcoes\"");

        if (item.has("colunas")) {
            if (!item.get("colunas").isSequence())
                throw FichaDefinitionError("campo \"" + c.id + "\": \"colunas\" deve ser uma lista");
            c.colunas = item.get("colunas").asStringList();
        }
        if (c.tipo == CampoTipo::Tabela && c.colunas.empty())
            throw FichaDefinitionError("campo \"" + c.id + "\": tipo \"tabela\" requer \"colunas\"");

        c.validacao = item.get("validacao").asString();
        if (!c.validacao.empty() && !isValidacaoConhecida(c.validacao))
            throw FichaDefinitionError("campo \"" + c.id + "\": validação desconhecida \"" + c.validacao + "\"");
        if (c.validacao == "soma_100" && c.tipo != CampoTipo::Tabela)
            throw FichaDefinitionError("campo \"" + c.id + "\": validação \"soma_100\" só se aplica a tipo \"tabela\"");

        std::string visivelSeStr = item.get("visivel_se").asString();
        if (!visivelSeStr.empty())
            c.visivelSe = parseVisivelSe(visivelSeStr);

        c.herdaDoProjeto = item.get("herda_do_projeto").asBool(false);
        c.preenchidoPor = item.get("preenchido_por").asString();
        c.sugeridoPor = item.get("sugerido_por").asString();

        int origens = (c.herdaDoProjeto ? 1 : 0) + (!c.preenchidoPor.empty() ? 1 : 0) + (!c.sugeridoPor.empty() ? 1 : 0);
        if (origens > 1)
            throw FichaDefinitionError("campo \"" + c.id + "\": herda_do_projeto, preenchido_por e sugerido_por são mutuamente exclusivos");

        if (item.has("afeta")) {
            if (!item.get("afeta").isSequence())
                throw FichaDefinitionError("campo \"" + c.id + "\": \"afeta\" deve ser uma lista");
            c.afeta = item.get("afeta").asStringList();
            for (auto& efeito : c.afeta)
                if (!isEfeitoConhecido(efeito))
                    throw FichaDefinitionError("campo \"" + c.id + "\": efeito desconhecido em \"afeta\": \"" + efeito + "\"");
        }

        c.alertaSeTrue = item.get("alerta_se_true").asString();
        if (!c.alertaSeTrue.empty() && c.tipo != CampoTipo::Booleano)
            throw FichaDefinitionError("campo \"" + c.id + "\": \"alerta_se_true\" só se aplica a tipo \"booleano\"");

        c.gerarEmSequencia = item.get("gerar_em_sequencia").asBool(false);

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

FichaDefinition load(const Node& root) {
    if (!root.isMap())
        throw FichaDefinitionError("a definição de ficha deve ser um mapa no nível raiz");

    FichaDefinition def;
    def.tipo = root.get("tipo").asString();
    def.rotulo = root.get("rotulo").asString();
    if (def.tipo.empty())
        throw FichaDefinitionError("\"tipo\" é obrigatório no nível raiz");
    if (def.rotulo.empty())
        throw FichaDefinitionError("\"rotulo\" é obrigatório no nível raiz");

    bool hasGrupos = root.has("grupos");
    bool hasNiveis = root.has("niveis");
    if (hasGrupos == hasNiveis)
        throw FichaDefinitionError("tipo \"" + def.tipo + "\": definição deve ter \"grupos\" OU \"niveis\", nunca os dois nem nenhum");

    if (hasNiveis) {
        const Node& niveisNode = root.get("niveis");
        if (!niveisNode.isSequence())
            throw FichaDefinitionError("tipo \"" + def.tipo + "\": \"niveis\" deve ser uma lista");
        def.niveis = niveisNode.asStringList();
        if (def.niveis.empty())
            throw FichaDefinitionError("tipo \"" + def.tipo + "\": \"niveis\" não pode ser vazio");
        if (def.niveis.size() > 2)
            throw FichaDefinitionError("tipo \"" + def.tipo + "\": este formato suporta no máximo dois níveis (raiz + um repetido)");

        for (auto& nivel : def.niveis) {
            const Node& bloco = root.get(nivel);
            if (!bloco.isMap())
                throw FichaDefinitionError("tipo \"" + def.tipo + "\": bloco do nível \"" + nivel + "\" ausente ou inválido");
            const Node& camposNode = bloco.get("campos");
            std::string contexto = "nível \"" + nivel + "\" do tipo \"" + def.tipo + "\"";
            std::vector<Campo> campos = parseCampos(camposNode, contexto);
            checkVisivelSeReferences(campos, contexto);
            def.camposPorNivel.emplace_back(nivel, std::move(campos));
        }
    } else {
        const Node& gruposNode = root.get("grupos");
        if (!gruposNode.isSequence())
            throw FichaDefinitionError("tipo \"" + def.tipo + "\": \"grupos\" deve ser uma lista");
        if (gruposNode.sequence.empty())
            throw FichaDefinitionError("tipo \"" + def.tipo + "\": \"grupos\" não pode ser vazio");

        std::vector<Campo> todosOsCamposDaRaiz; // para checar unicidade de id entre grupos
        for (auto& g : gruposNode.sequence) {
            if (!g.isMap())
                throw FichaDefinitionError("tipo \"" + def.tipo + "\": cada grupo deve ser um mapa");
            Grupo grupo;
            grupo.rotulo = g.get("rotulo").asString();
            if (grupo.rotulo.empty())
                throw FichaDefinitionError("tipo \"" + def.tipo + "\": grupo sem \"rotulo\"");
            grupo.campos = parseCampos(g.get("campos"), "grupo \"" + grupo.rotulo + "\" do tipo \"" + def.tipo + "\"");
            for (auto& c : grupo.campos)
                todosOsCamposDaRaiz.push_back(c);
            def.grupos.push_back(std::move(grupo));
        }
        checkIdsUnicos(todosOsCamposDaRaiz, "tipo \"" + def.tipo + "\" (ids devem ser únicos entre todos os grupos)");
        checkVisivelSeReferences(todosOsCamposDaRaiz, "tipo \"" + def.tipo + "\" (todos os grupos)");
    }

    if (root.has("arquivos_esperados")) {
        const Node& arqNode = root.get("arquivos_esperados");
        if (!arqNode.isSequence())
            throw FichaDefinitionError("tipo \"" + def.tipo + "\": \"arquivos_esperados\" deve ser uma lista");
        for (auto& item : arqNode.sequence) {
            if (!item.isMap())
                throw FichaDefinitionError("tipo \"" + def.tipo + "\": cada entrada de \"arquivos_esperados\" deve ser um mapa");
            ArquivoEsperado ae;
            ae.papel = item.get("papel").asString();
            if (ae.papel.empty())
                throw FichaDefinitionError("tipo \"" + def.tipo + "\": entrada de \"arquivos_esperados\" sem \"papel\"");
            ae.obrigatorio = item.get("obrigatorio").asBool(false);
            ae.por = item.get("por").asString();
            if (!ae.por.empty() && std::find(def.niveis.begin(), def.niveis.end(), ae.por) == def.niveis.end())
                throw FichaDefinitionError("tipo \"" + def.tipo + "\": arquivos_esperados[" + ae.papel + "].por = \"" + ae.por + "\" não é um nível declarado em \"niveis\"");
            ae.minimo = item.get("minimo").asString();
            def.arquivosEsperados.push_back(std::move(ae));
        }
    }

    return def;
}

FichaDefinition loadFromFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw FichaDefinitionError("não foi possível abrir o arquivo: " + path);
    std::ostringstream buffer;
    buffer << file.rdbuf();

    try {
        matriz::ficha_yaml::Node root = matriz::ficha_yaml::parse(buffer.str());
        return load(root);
    } catch (const matriz::ficha_yaml::YamlError& e) {
        throw FichaDefinitionError(path + ": erro de YAML: " + e.what());
    } catch (const FichaDefinitionError& e) {
        throw FichaDefinitionError(path + ": " + e.what());
    }
}

} // namespace matriz::ficha
