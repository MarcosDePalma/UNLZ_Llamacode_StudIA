#pragma once

// Detector de voz (VAD) con piso de ruido adaptativo, histéresis y hangover.
//
// Reemplaza al VAD de umbral RMS fijo (vadThreshold/vadActivationLevel), que
// falla en los dos extremos: con mic ruidoso todo frame pasa como voz (el turno
// no cierra nunca) y con mic bajo la voz real no llega al umbral (el turno se
// descarta como "sin voz útil"). Acá el umbral se calcula CONTRA el ruido de
// fondo medido en vivo, así que la sensibilidad se ajusta sola al micrófono.
//
// Tres piezas, todas puras (sin Qt, sin audio: entra el RMS del frame):
//  - Piso adaptativo: sigue el ruido de fondo. Baja rápido (silencio nuevo, más
//    bajo, se cree enseguida) y sube lento (para que la voz sostenida no se
//    coma su propio piso). Solo se actualiza cuando NO hay voz.
//  - Histéresis: entrar a voz exige onFactor×piso; salir solo baja de
//    offFactor×piso (offFactor < onFactor). Sin esto un frame en el borde
//    hace flapping speech/silencio.
//  - Hangover: tras el último frame con voz se sigue considerando voz por
//    hangoverMs. Cubre las oclusivas ("p", "t") y las micro-pausas dentro de
//    una palabra, que si no cortarían el segmento a mitad.
//
// El seam para un backend neuronal (Silero VAD por ONNX) es push(): entra un
// escalar por frame y sale una decisión. Un VadEngine que en vez de RMS+piso
// consulte el modelo mantiene la misma interfaz y el resto no se entera.
struct VadTuning {
    // Umbral de entrada a voz = piso × onFactor (nunca menor a absFloor).
    double onFactor  = 3.0;
    // Umbral de salida = piso × offFactor. Menor que onFactor → histéresis.
    double offFactor = 1.8;
    // Piso mínimo asumido: evita que en silencio digital (piso→0) cualquier
    // click supere el umbral relativo.
    double absFloor  = 0.004;
    // Suavizado del piso: rápido cuando el frame está por debajo del piso,
    // lento cuando está por encima. [0..1], mayor = sigue más rápido.
    double floorDown = 0.5;
    double floorUp   = 0.02;
    // Voz sostenida tras el último frame por encima del umbral de salida.
    int    hangoverMs = 220;
    // Frames de voz consecutivos exigidos para declarar voz (anti-click).
    int    onsetFrames = 2;
};

class VadEngine
{
public:
    // Resultado del frame. `level` es el RMS crudo (meter de UI), `floor` el
    // ruido de fondo estimado y `snr` cuántas veces el piso es el frame (la
    // medida real de "hay voz"), útil para logs de diagnóstico.
    struct Frame {
        bool   speech = false;   // el frame cuenta como voz (ya con hangover)
        bool   onset  = false;   // transición silencio → voz en este frame
        double level  = 0.0;
        double floor  = 0.0;
        double snr    = 0.0;
    };

    explicit VadEngine(const VadTuning &t = VadTuning());

    void setTuning(const VadTuning &t) { m_t = t; }
    const VadTuning &tuning() const { return m_t; }

    // Vuelve al estado inicial (piso sin sembrar). Llamar al abrir captura.
    void reset();

    // Procesa un frame de `frameMs` con energía RMS `rms` [0..1].
    Frame push(double rms, int frameMs);

    bool   speaking() const { return m_speech; }
    double noiseFloor() const { return m_floor; }
    // ¿Hubo voz desde el último reset()? Equivale al viejo peak>=activation.
    bool   sawSpeech() const { return m_sawSpeech; }

private:
    VadTuning m_t;
    double m_floor    = 0.0;
    bool   m_seeded   = false;  // el piso todavía no vio un frame
    bool   m_speech   = false;
    bool   m_sawSpeech = false;
    int    m_hangMs   = 0;      // hangover restante
    int    m_onsetRun = 0;      // frames consecutivos por encima del umbral on
};
