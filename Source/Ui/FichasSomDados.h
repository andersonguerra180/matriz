#pragma once
// Gerado a partir de fichas/som_contexto.json (fichas de contextualização por tipo de som, EN/PT). Não editar à mão.
#include <vector>

namespace matriz::ui::mini::fichasom {

struct Opcao { const char* id; const char* en; const char* pt; };
struct Campo {
    const char* id; const char* labelEn; const char* labelPt; const char* dicaEn; const char* dicaPt;
    int entrada;      // 0 = texto, 1 = escolha, 2 = texto longo
    bool opcional;
    std::vector<Opcao> opcoes;
};
struct Tipo { const char* id; const char* en; const char* pt; std::vector<Campo> campos; };

inline const std::vector<Campo>& comuns() {
    static const std::vector<Campo> v = {
        {"source_media","Source media","Origem","","",1,false,{{"native_digital_file","Native digital file","Arquivo digital original"},{"memory_card","Memory card","Cartão de memória"},{"cd","CD","CD"},{"tape","Tape","Fita"},{"vinyl","Vinyl","Vinil"},{"phone","Phone","Celular"},{"other","Other","Outro"},}},
        {"media_id","Card, CD or tape ID","ID do cartão, CD ou fita","Label, number or name written on it","Etiqueta, número ou nome escrito nele",0,false,{}},
        {"context","Context / Additional Details","Contexto / Detalhes adicionais","","",0,false,{}},
    };
    return v;
}

inline const std::vector<Tipo>& tipos() {
    static const std::vector<Tipo> v = {
        {"none","(none)","(nenhum)",{
        }},
        {"voice_memo","Voice memo","Nota de voz",{
            {"subject","Subject","Assunto","","",0,false,{}},
            {"date_place","Date and place","Data e local","","",0,true,{}},
        }},
        {"voice_over","Voice over","Locução",{
            {"project","Project","Projeto","","",0,false,{}},
            {"voice_talent","Voice talent","Locutor(a)","","",0,false,{}},
            {"language","Language","Idioma","","",0,false,{}},
            {"version","Version","Versão","","",1,false,{{"draft","Draft","Rascunho"},{"final","Final","Final"},}},
            {"recording_location","Recording location","Local da gravação","","",0,false,{}},
        }},
        {"audiobook","Audiobook","Audiolivro",{
            {"book_title","Book title","Título do livro","","",0,false,{}},
            {"author_narrator","Author and narrator","Autor(a) e narrador(a)","","",0,false,{}},
            {"chapter","Chapter or part","Capítulo ou parte","","",0,false,{}},
        }},
        {"sample_pack","Sample pack","Pacote de sons",{
            {"pack_name","Pack name","Nome do pacote","","",0,false,{}},
            {"source","Source","Fonte","","",1,false,{{"bought","Bought","Comprado"},{"free","Free","Gratuito"},{"mine","Made by me","Feito por mim"},}},
            {"sound_type","Sound type","Tipo de som","","",0,false,{}},
        }},
        {"midi","MIDI","MIDI",{
            {"song_name","Song or idea name","Nome da música ou ideia","","",0,false,{}},
            {"instrument","Instrument or part","Instrumento ou parte","","",0,false,{}},
            {"tempo_key","Tempo and key","Andamento e tom","","",0,true,{}},
        }},
        {"soundtrack","Soundtrack","Trilha sonora",{
            {"soundtrack_type","Soundtrack type","Tipo de trilha","","",1,false,{{"film","Film","Filme"},{"game","Game","Jogo"},{"web","Web","Web"},{"radio","Radio","Rádio"},{"tv","TV","TV"},{"other","Other","Outro"},}},
            {"project","Title or project","Título ou projeto","","",0,false,{}},
            {"scene","Scene or moment","Cena ou momento","","",0,false,{}},
            {"composer","Composer","Compositor(a)","","",0,false,{}},
        }},
        {"field_recording","Field recording","Gravação de campo",{
            {"place","Place","Local","","",0,false,{}},
            {"date_time","Date and time","Data e hora","","",0,false,{}},
            {"what_recorded","What was recorded","O que foi gravado","","",0,false,{}},
            {"gear","Gear used","Equipamento usado","","",0,true,{}},
            {"log_notes","Log Notes","Notas de registro","","",2,true,{}},
        }},
        {"music","Music","Música",{
            {"artist","Artist or band","Artista ou banda","","",0,false,{}},
            {"title","Song or album title","Título da música ou álbum","","",0,false,{}},
            {"year","Year","Ano","","",0,false,{}},
            {"kind","Kind","Tipo","","",1,false,{{"album","Album","Disco"},{"single","Single","Single"},{"ep","EP","EP"},{"demo","Demo","Demo"},{"live","Live","Ao vivo"},}},
        }},
        {"cd_rip","CD rip","Cópia de CD",{
            {"cd_label","Number or name on the CD","Número ou nome escrito no CD","","",0,false,{}},
            {"artist_album","Artist and album","Artista e álbum","","",0,false,{}},
            {"condition","Disc condition","Estado do CD","","",1,false,{{"good","Good","Bom"},{"scratched","Scratched","Riscado"},}},
        }},
        {"ddp_master","DDP master (CD)","Master DDP (CD)",{
            {"artist_album","Artist and album","Artista e álbum","","",0,false,{}},
        }},
        {"vinyl_rip","Vinyl rip","Cópia de vinil",{
            {"artist_album","Artist and album","Artista e álbum","","",0,false,{}},
            {"side","Side","Lado","","",1,false,{{"both","Both","Ambos"},{"a","Side A","Lado A"},{"b","Side B","Lado B"},}},
            {"speed","Speed (RPM)","Rotação (RPM)","","",1,false,{{"33","33","33"},{"45","45","45"},}},
            {"condition","Record condition","Estado do disco","","",1,false,{{"good","Good","Bom"},{"noisy","Noisy","Chiado"},{"scratched","Scratched","Riscado"},}},
        }},
        {"music_production","Music Production","Produção musical",{
            {"artist","Artist or band","Artista ou banda","","",0,false,{}},
            {"project","Project or song name","Nome do projeto ou música","","",0,false,{}},
            {"release_type","Release type","Tipo de lançamento","","",1,false,{{"single","Single","Single"},{"ep","EP","EP"},{"album","Album","Álbum"},{"demo","Demo","Demo"},{"live","Live","Ao vivo"},}},
            {"stage","Stage","Etapa","","",1,false,{{"recording","Recording","Gravação"},{"mixing","Mixing","Mixagem"},{"mastering","Mastering","Masterização"},{"final","Final","Final"},}},
            {"recording_medium","Recording medium","Mídia de gravação","","",1,false,{{"daw","DAW","DAW"},{"tape_quarter","1/4\" Tape","Fita 1/4\""},{"tape_half","1/2\" Tape","Fita 1/2\""},{"tape_1in","1\" Tape","Fita 1\""},{"tape_2in","2\" Tape","Fita 2\""},{"cassette","Cassette Tape","Fita Cassete"},{"dat","DAT","DAT (Fita de Áudio Digital)"},{"adat","ADAT","ADAT"},{"dtrs","DTRS / Hi8 Digital Audio Tape","Fita DTRS / Hi8 Digital Audio"},{"minidisc","MiniDisc (MD)","MiniDisc (MD)"},{"audio_cd","Audio CD / CD-R","CD de Áudio / CD-R"},}},
            {"description","Description","Descrição","","",2,true,{}},
        }},
    };
    return v;
}

} // namespace matriz::ui::mini::fichasom
