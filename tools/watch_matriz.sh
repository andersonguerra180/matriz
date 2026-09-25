#!/bin/bash
# watch_matriz.sh — monitor externo do BKR Matriz, rodado no Terminal.
#
# Roda FORA do app de propósito: se o app travar ou crashar, este script
# continua vivo e continua capturando. Loga CPU/memória/threads periodicamente
# e, assim que o macOS marca o processo como "not responding" (o mesmo estado
# que mostra a bolinha colorida girando), dispara automaticamente um `sample`
# nativo do macOS — uma captura de pilha de TODAS as threads no exato momento
# do travamento, sem precisar esperar o app crashar sozinho pra gerar um
# relatório.
#
# Uso:
#   ./tools/watch_matriz.sh
# (deixa rodando numa aba do Terminal enquanto usa o app normalmente; feche
#  com Ctrl+C quando quiser parar)
#
# Ao terminar (ou quando algo travar), mande o conteúdo de:
#   ~/matriz_diagnostics/monitor.log
#   ~/matriz_diagnostics/sample_*.txt   (um arquivo por travamento capturado)

set -u

APP_NOME="BKR Matriz"
DIAG_DIR="$HOME/matriz_diagnostics"
LOG_FILE="$DIAG_DIR/monitor.log"
INTERVALO=3          # segundos entre checagens de CPU/memória/responsividade
SEGUNDOS_SAMPLE=5     # duração de cada captura de pilha (`sample`) ao detectar travamento
COOLDOWN_SAMPLE=30    # não repete outra captura de pilha antes desse tempo (evita spam se ficar travado)

mkdir -p "$DIAG_DIR"

log() {
    printf '[%s] %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$1" | tee -a "$LOG_FILE"
}

log "=== watch_matriz.sh iniciado — logs em $DIAG_DIR ==="

ultimo_sample_epoch=0

esperar_app() {
    while true; do
        pid=$(pgrep -f "$APP_NOME.app/Contents/MacOS/$APP_NOME" | head -n1)
        if [ -n "${pid:-}" ]; then
            echo "$pid"
            return
        fi
        sleep 2
    done
}

while true; do
    PID=$(esperar_app)
    log "App detectado — PID $PID"

    while kill -0 "$PID" 2>/dev/null; do
        # CPU%, memória residente (KB) e nº de threads (macOS: coluna TH do
        # top). `-l 1` (uma amostra só) sempre reporta 0.0% de CPU — o top
        # precisa de duas amostras pra calcular uma taxa; `-l 2` descarta a
        # primeira (sempre zerada) e usa só a segunda, que já tem uma janela
        # real pra medir.
        stats=$(top -l 2 -s 1 -pid "$PID" -stats pid,cpu,th,mem 2>/dev/null | tail -n1)

        # "not responding" é o MESMO sinal que o macOS usa pra mostrar a
        # bolinha colorida girando — checagem via System Events, não custa
        # quase nada e é o indicador mais confiável de travamento real.
        nao_responde=$(osascript -e '
            tell application "System Events"
                try
                    set p to first application process whose unix id is '"$PID"'
                    return not responding of p
                on error
                    return "unknown"
                end try
            end tell' 2>/dev/null)

        log "PID=$PID  stats=[$stats]  not_responding=$nao_responde"

        if [ "$nao_responde" = "true" ]; then
            agora=$(date +%s)
            if [ $((agora - ultimo_sample_epoch)) -ge "$COOLDOWN_SAMPLE" ]; then
                ultimo_sample_epoch=$agora
                sample_file="$DIAG_DIR/sample_$(date '+%Y%m%d_%H%M%S').txt"
                log "!!! TRAVAMENTO DETECTADO — capturando pilha de todas as threads por ${SEGUNDOS_SAMPLE}s em $sample_file"
                # `sample` é uma ferramenta nativa do macOS (Command Line
                # Tools) — não mata nem pausa o processo, só observa.
                sample "$PID" "$SEGUNDOS_SAMPLE" -f "$sample_file" >/dev/null 2>&1
                log "Captura salva: $sample_file"
            fi
        fi

        sleep "$INTERVALO"
    done

    log "App encerrado (PID $PID não existe mais) — voltando a esperar o app abrir de novo."
done
