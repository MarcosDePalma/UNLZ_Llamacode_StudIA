#pragma once
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

// Un fragmento de documentacion academica recuperado del indice.
struct StudiaFragmento {
    int     fragId = 0;  // id del fragmento; clave para fusionar rankings
    int     docId = 0;
    QString documento;   // nombre del archivo original
    QString materia;
    QString anio;        // 1A .. 5A
    QString ruta;        // ruta absoluta al original (para abrirlo desde la UI)
    int     pagina = 0;
    double  score = 0.0; // bm25 de SQLite: mas negativo = mas relevante
    QString texto;
    // true = salio de la bibliografia que subio el estudiante, no del corpus
    // de la catedra. Se muestra distinto en las citas.
    bool    propio = false;
};

// Acceso de SOLO LECTURA al indice documental que genera
// `tools/studia/ingest.py`. No escribe nunca en la base: la ingesta es un
// proceso aparte, offline.
//
// La busqueda usa FTS5 con ranking BM25. El tokenizador del indice se creo con
// `remove_diacritics 2`, asi que "mecanica" y "mecanica" matchean igual.
class StudiaIndex
{
public:
    StudiaIndex() = default;
    ~StudiaIndex();
    StudiaIndex(const StudiaIndex &) = delete;
    StudiaIndex &operator=(const StudiaIndex &) = delete;

    // Abre el indice. Falla si el archivo no existe o no tiene el esquema
    // esperado (deja el motivo en *err).
    bool abrir(const QString &dbPath, QString *err = nullptr);
    void cerrar();
    bool abierto() const { return m_abierto; }
    QString rutaDb() const { return m_ruta; }
    // Carpeta desde la que se ingesto este indice. Los documentos guardan su
    // ruta absoluta; con esta se puede recalcular la parte relativa y
    // encontrarlos en otra maquina. Vacia en indices viejos, anteriores a que
    // el ingestor la registrara.
    QString corpusRaiz() const { return m_corpusRaiz; }

    // Recupera hasta `k` fragmentos relevantes. Si `materia` no esta vacia,
    // acota la busqueda a esa materia. Limita a `maxPorDoc` fragmentos del
    // mismo documento para que la respuesta no se apoye en una sola fuente.
    //
    // Devuelve VACIO cuando la pregunta no esta cubierta por el corpus (ver
    // `hayEvidencia`). Que no haya resultados es una respuesta valida: el
    // controlador la traduce en la frase de abstencion.
    QVector<StudiaFragmento> buscar(const QString &consulta, int k = 6,
                                    const QString &materia = QString(),
                                    int maxPorDoc = 3) const;

    // Umbral de abstencion sobre el score BM25 normalizado por la cantidad de
    // terminos DISCRIMINANTES (bm25 de SQLite es negativo: cuanto MAS negativo,
    // mejor). Si el mejor resultado no lo supera, no hay evidencia.
    //
    // El default -7.0 sale de medir 30 preguntas escritas en lenguaje natural
    // contra el corpus real de la PPS: responde las 16 cubiertas y se abstiene
    // en las 14 ajenas. Recalibrar con cada corpus nuevo.
    void setUmbralAbstencion(double u) { m_umbral = u; }
    double umbralAbstencion() const { return m_umbral; }

    // Desactiva la regla de abstencion para este indice. Se usa en el indice de
    // bibliografia PROPIA: ahi el gate no aplica por dos razones.
    //  1. BM25 pesa cada termino por lo raro que es en el corpus. En un indice
    //     de 3 fragmentos todos los terminos aparecen en todos, el peso da 0 y
    //     el score queda en -0.00: con el umbral calibrado para 139.000
    //     fragmentos, el material adjuntado NUNCA superaria el corte.
    //  2. El estudiante subio ese documento a proposito, y el corpus de la
    //     catedra ya aporta la garantia de no responder sin evidencia.
    void setExigirEvidencia(bool exigir) { m_exigirEvidencia = exigir; }
    bool exigirEvidencia() const { return m_exigirEvidencia; }

    // ¿Los candidatos alcanzan para responder? Estatica y pura para poder
    // testear la regla sin base de datos.
    //  - `mejorScore`: bm25 crudo del mejor fragmento.
    //  - `terminos`: los de la consulta.
    //  - `discriminantes`: cuantos de esos terminos son especificos (ver
    //    esDiscriminante). Es el divisor del score; 0 = abstenerse.
    //  - `textosCandidatos`: los mejores fragmentos (se miran los 3 primeros).
    static bool hayEvidencia(double mejorScore, const QStringList &terminos,
                             int discriminantes,
                             const QStringList &textosCandidatos, double umbral);

    // Un termino discrimina si existe en el corpus y no es tan frecuente como
    // para aparecer en todos lados. Dividir el score por la cantidad TOTAL de
    // terminos castigaba a las preguntas naturales: "explicame de que se trata
    // un motor a induccion" arrastra relleno que no aporta al ranking pero
    // agranda el divisor, y la pregunta terminaba rechazada aunque el material
    // estuviera. Dividir solo por los discriminantes lo corrige.
    static bool esDiscriminante(int frecuenciaDocumental, int totalFragmentos);
    static constexpr double kFraccionFrecuente = 0.01;   // 1% del corpus
    // Piso para indices chicos: sin esto, el 1% de un corpus de 200 fragmentos
    // da 2 y practicamente ningun termino calificaria como discriminante, con
    // lo cual el asistente se abstendria siempre.
    static constexpr int kMinFrecuenteAbsoluto = 50;

    // Cuantos fragmentos del tope se miran para la regla de cobertura. Medido
    // sobre el corpus real: mirar 1 o 3 da el mismo resultado, pero mirar 3
    // evita abstenerse cuando el primer fragmento engancha flojo y el segundo no.
    static constexpr int kCandidatosCobertura = 3;

    // En cuantos fragmentos aparece un termino (cacheado por instancia).
    int frecuenciaDocumental(const QString &termino) const;
    int totalFragmentos() const;

    // Terminos utiles de una pregunta (minusculas, sin palabras vacias ni
    // tokens de 1-2 caracteres, sin repetidos).
    static QStringList terminosConsulta(const QString &texto);

    // Documentos por estado, fragmentos y paginas. Si `materia` no esta vacia,
    // cuenta SOLO esa materia (es lo que se muestra al elegirla en la UI: al
    // estudiante le importa cuanto material tiene de Calculo 2, no el total).
    QVariantMap estadisticas(const QString &materia = QString()) const;
    QStringList materias() const;       // materias con al menos un fragmento

    // ── Busqueda semantica ───────────────────────────────────────────────────
    // ¿El indice tiene vectores? (los genera tools/studia/vectorizar.py). Si no
    // los tiene, buscar() usa solo BM25 y todo sigue funcionando.
    bool tieneVectores() const;
    int cantidadVectores() const;
    int dimensionVectores() const;

    // Busqueda HIBRIDA: fusiona el ranking lexico (BM25) con el semantico
    // (coseno contra `vectorConsulta`) por Reciprocal Rank Fusion.
    //
    // Se fusionan RANKINGS y no puntajes porque BM25 y el coseno viven en
    // escalas distintas y no son comparables. RRF sólo mira en qué posición
    // quedó cada fragmento en cada lista.
    //
    // Si `vectorConsulta` viene vacio o el indice no tiene vectores, equivale a
    // buscar() lexico.
    QVector<StudiaFragmento> buscarHibrido(const QString &consulta,
                                           const QVector<float> &vectorConsulta,
                                           int k = 6,
                                           const QString &materia = QString(),
                                           int maxPorDoc = 3) const;

    // Constante de amortiguacion de RRF. 60 es el valor clasico del paper y el
    // que ya usa el agente de LlamaCode en su hybrid_search.
    static constexpr double kRrfK = 60.0;
    // Cuantos candidatos se traen de cada ranking antes de fusionar.
    static constexpr int kCandidatosPorRanking = 40;

    // Documentos del indice (para listar la bibliografia propia en la UI).
    // Si `materia` no esta vacia, sólo los de esa materia.
    // Cada entrada: {nombre, ruta, materia, estado, paginas, fragmentos}.
    QVariantList documentos(const QString &materia = QString()) const;

    // Traduce una pregunta en lenguaje natural a una expresion FTS5 valida:
    // baja a minusculas, descarta palabras vacias y terminos de 1-2 letras,
    // entrecomilla cada termino y los une con OR. Devuelve "" si no queda nada
    // util (por ejemplo "y el de la"). Estatica para poder testearla sin base.
    static QString consultaFts(const QString &texto);
    static bool esPalabraVacia(const QString &termino);
    // Version sin tildes ni mayusculas, para comparar contra la lista de
    // palabras vacias.
    static QString normalizar(const QString &s);

private:
    QString m_conn;      // nombre de la conexion QSqlDatabase (unico por instancia)
    QString m_ruta;
    QString m_corpusRaiz;
    bool    m_abierto = false;
    double  m_umbral = -7.0;
    bool    m_exigirEvidencia = true;
    // Caches de solo lectura: el indice no cambia mientras esta abierto.
    mutable QHash<QString, int> m_dfCache;
    mutable int m_totalFragmentos = -1;
};
