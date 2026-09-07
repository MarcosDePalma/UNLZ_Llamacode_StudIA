#pragma once
#include <QString>

// Endpointing: ¿cuánto silencio hay que esperar para dar el turno por cerrado?
//
// El VAD sabe si hay energía, no si la persona TERMINÓ de hablar. Con un
// silencio fijo (vadSilenceMs) se paga siempre el peor caso: si esperás poco,
// cortás a la mitad de "quiero que busques… (pensando) …el log de ayer"; si
// esperás mucho, cada frase cerrada arrastra ~1s de latencia muerta.
//
// Acá el umbral es función del transcript parcial que el STT ya devolvió del
// turno en curso (transcripción incremental por segmentos). Misma idea que el
// Smart-Turn de OpenLive (endpointing semántico), resuelta sobre el texto en
// vez de sobre prosodia — sin modelo ni dependencia nueva:
//   - la frase cerró (punto, signo de cierre, o pinta de completa) → cortar
//     antes (más responsivo).
//   - quedó colgada ("y", "porque", "eh", "o sea", "para") → esperar más: la
//     persona está pensando en voz alta, no terminó.
//   - nada concluyente → el silencio base configurado.
//
// Todo puro y sin estado: entra texto, sale un número de ms.
namespace TurnDetector {

// Clasificación del final del parcial (expuesta para tests/diagnóstico).
enum Ending {
    Unknown,      // ni cerrado ni colgado: usar el silencio base
    Complete,     // terminador de oración o frase con pinta de cerrada
    Incomplete    // conjunción/muletilla/preposición colgando: sigue hablando
};

// Cómo se estira o encoge el silencio base según el final detectado.
struct EndpointTuning {
    double completeFactor   = 0.45;   // frase cerrada → recortar
    double incompleteFactor = 1.6;    // frase colgada → estirar
    int    minMs = 250;               // nunca cortar más rápido que esto
    int    maxMs = 2500;              // ni esperar más que esto
};

// Clasifica el final del texto parcial.
Ending classify(const QString &partial);

// Silencio (ms) exigido para cerrar el turno, dado el parcial y el base.
// partial vacío (el STT todavía no devolvió nada) → baseMs sin tocar.
int requiredSilenceMs(const QString &partial, int baseMs,
                      const EndpointTuning &t = EndpointTuning());

} // namespace TurnDetector
