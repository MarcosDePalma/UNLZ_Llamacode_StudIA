#include "StudiaIndex.h"

#include <QDateTime>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>

#include <memory>

namespace {

// Palabras vacias del castellano + verbos tipicos de una consulta ("explicame",
// "dame"). No aportan a la busqueda y, al ser muy frecuentes, ensucian el
// ranking. Se comparan sin tildes (ver StudiaIndex::normalizar).
const QSet<QString> &palabrasVacias()
{
    static const QSet<QString> s = {
        QStringLiteral("a"), QStringLiteral("al"), QStringLiteral("ante"),
        QStringLiteral("con"), QStringLiteral("como"), QStringLiteral("cual"),
        QStringLiteral("cuales"), QStringLiteral("cuando"), QStringLiteral("de"),
        QStringLiteral("del"), QStringLiteral("desde"), QStringLiteral("donde"),
        QStringLiteral("dos"), QStringLiteral("el"), QStringLiteral("ella"),
        QStringLiteral("ellos"), QStringLiteral("en"), QStringLiteral("entre"),
        QStringLiteral("era"), QStringLiteral("es"), QStringLiteral("esa"),
        QStringLiteral("ese"), QStringLiteral("eso"), QStringLiteral("esta"),
        QStringLiteral("estas"), QStringLiteral("este"), QStringLiteral("esto"),
        QStringLiteral("estos"), QStringLiteral("fue"), QStringLiteral("ha"),
        QStringLiteral("han"), QStringLiteral("hasta"), QStringLiteral("hay"),
        QStringLiteral("la"), QStringLiteral("las"), QStringLiteral("le"),
        QStringLiteral("les"), QStringLiteral("lo"), QStringLiteral("los"),
        QStringLiteral("mas"), QStringLiteral("me"), QStringLiteral("mi"),
        QStringLiteral("muy"), QStringLiteral("no"), QStringLiteral("nos"),
        QStringLiteral("o"), QStringLiteral("para"), QStringLiteral("pero"),
        QStringLiteral("por"), QStringLiteral("porque"), QStringLiteral("que"),
        QStringLiteral("quien"), QStringLiteral("se"), QStringLiteral("ser"),
        QStringLiteral("si"), QStringLiteral("sin"), QStringLiteral("sobre"),
        QStringLiteral("son"), QStringLiteral("su"), QStringLiteral("sus"),
        QStringLiteral("te"), QStringLiteral("tu"), QStringLiteral("un"),
        QStringLiteral("una"), QStringLiteral("unas"), QStringLiteral("uno"),
        QStringLiteral("unos"), QStringLiteral("y"), QStringLiteral("ya"),
        // Formulas de pedido: aparecen en casi toda pregunta y no discriminan.
        QStringLiteral("explicame"), QStringLiteral("explica"), QStringLiteral("explicar"),
        QStringLiteral("decime"), QStringLiteral("dame"), QStringLiteral("contame"),
        QStringLiteral("quiero"), QStringLiteral("necesito"), QStringLiteral("podes"),
        QStringLiteral("puedo"), QStringLiteral("hacer"), QStringLiteral("sobre"),
        // Muletillas de pregunta natural. Sin estas, "explicame de que se
        // trata un motor a induccion" arrastraba "trata" como si fuera un
        // termino tecnico y la pregunta quedaba rechazada.
        QStringLiteral("trata"), QStringLiteral("tratan"), QStringLiteral("tratar"),
        QStringLiteral("consiste"), QStringLiteral("consisten"),
        QStringLiteral("significa"), QStringLiteral("significan"),
        QStringLiteral("sirve"), QStringLiteral("sirven"),
        QStringLiteral("funciona"), QStringLiteral("funcionan"),
        QStringLiteral("define"), QStringLiteral("definir"),
        QStringLiteral("entender"), QStringLiteral("comprender"),
        QStringLiteral("saber"), QStringLiteral("cuenta"), QStringLiteral("contar"),
        QStringLiteral("hablame"), QStringLiteral("comentame"),
        QStringLiteral("resumime"), QStringLiteral("resumir"), QStringLiteral("resume"),
        QStringLiteral("describe"), QStringLiteral("describir"),
        QStringLiteral("explicacion"), QStringLiteral("tema"), QStringLiteral("temas"),
        QStringLiteral("cosa"), QStringLiteral("cosas"), QStringLiteral("algo"),
        QStringLiteral("alguien"), QStringLiteral("manera"), QStringLiteral("forma"),
        QStringLiteral("modo"), QStringLiteral("tipo"), QStringLiteral("tipos"),
        QStringLiteral("caso"), QStringLiteral("casos"), QStringLiteral("vez"),
        QStringLiteral("veces"), QStringLiteral("tiene"), QStringLiteral("tienen"),
        QStringLiteral("tener"), QStringLiteral("hace"), QStringLiteral("hacen"),
        QStringLiteral("haces"), QStringLiteral("debe"), QStringLiteral("deben"),
        QStringLiteral("deberia"), QStringLiteral("estan"), QStringLiteral("estamos"),
        QStringLiteral("somos"), QStringLiteral("soy"), QStringLiteral("mucho"),
        QStringLiteral("mucha"), QStringLiteral("muchos"), QStringLiteral("muchas"),
        QStringLiteral("poco"), QStringLiteral("poca"), QStringLiteral("todo"),
        QStringLiteral("toda"), QStringLiteral("todos"), QStringLiteral("todas"),
        QStringLiteral("otro"), QStringLiteral("otra"), QStringLiteral("otros"),
        QStringLiteral("otras"), QStringLiteral("mismo"), QStringLiteral("misma"),
        QStringLiteral("cada"), QStringLiteral("cuanto"), QStringLiteral("cuanta"),
        QStringLiteral("cuantos"), QStringLiteral("cuantas"), QStringLiteral("adonde"),
        QStringLiteral("ademas"), QStringLiteral("tambien"), QStringLiteral("entonces"),
        QStringLiteral("asi"), QStringLiteral("ahora"), QStringLiteral("despues"),
        QStringLiteral("antes"), QStringLiteral("luego"), QStringLiteral("bien"),
        QStringLiteral("mal"), QStringLiteral("mejor"), QStringLiteral("peor"),
        QStringLiteral("grande"), QStringLiteral("chico"),
        // Verbos de "¿qué pasa si…?". Son sinónimos entre sí pero BM25 los pesa
        // distinto según cuán frecuentes sean en el corpus, y eso hacía que
        // "¿qué pasa si aumento la frecuencia?" y "¿qué sucede si aumento la
        // frecuencia?" se comportaran distinto. Sacándolos, las dos formas de
        // preguntar producen la misma consulta. NO se listan verbos con carga
        // técnica (genera, produce, afecta, influye, varía): esos sí importan.
        QStringLiteral("pasa"), QStringLiteral("pasan"), QStringLiteral("paso"),
        QStringLiteral("sucede"), QStringLiteral("suceden"), QStringLiteral("sucedio"),
        QStringLiteral("ocurre"), QStringLiteral("ocurren"), QStringLiteral("ocurrio"),
        QStringLiteral("anda"), QStringLiteral("andan"),
        QStringLiteral("queda"), QStringLiteral("quedan"),
        QStringLiteral("resulta"), QStringLiteral("resultan"),
        QStringLiteral("existe"), QStringLiteral("existen"),
    };
    return s;
}

// Tope de terminos que se mandan a FTS5. Mas que esto no mejora el ranking y
// alarga la consulta sin necesidad.
constexpr int kMaxTerminos = 12;

}  // namespace


StudiaIndex::~StudiaIndex() { cerrar(); }

QString StudiaIndex::normalizar(const QString &s)
{
    QString out = s.normalized(QString::NormalizationForm_D);
    out.remove(QRegularExpression(QStringLiteral("[\\x{0300}-\\x{036f}]")));
    return out.toLower();
}

bool StudiaIndex::esPalabraVacia(const QString &termino)
{
    return palabrasVacias().contains(normalizar(termino));
}

QStringList StudiaIndex::terminosConsulta(const QString &texto)
{
    static const QRegularExpression sep(QStringLiteral("[^\\p{L}\\p{N}_]+"));
    QStringList terminos;
    QSet<QString> vistos;
    for (const QString &crudo : texto.split(sep, Qt::SkipEmptyParts)) {
        const QString t = crudo.toLower();
        // Los terminos de 1-2 caracteres casi siempre son ruido ("de", "el",
        // "s1"). Se conservan los numericos largos (ej. "485" de RS-485).
        if (t.size() < 3)
            continue;
        if (esPalabraVacia(t))
            continue;
        const QString clave = normalizar(t);
        if (vistos.contains(clave))
            continue;
        vistos.insert(clave);
        terminos << t;
        if (terminos.size() >= kMaxTerminos)
            break;
    }
    return terminos;
}

QString StudiaIndex::consultaFts(const QString &texto)
{
    QStringList entrecomillados;
    for (const QString &t : terminosConsulta(texto)) {
        // Entrecomillar aisla el termino de la sintaxis de FTS5 (AND/OR/NOT/*).
        // Las comillas internas se escapan duplicandolas.
        QString escapado = t;
        escapado.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        entrecomillados << QStringLiteral("\"%1\"").arg(escapado);
    }
    // OR y no AND: una pregunta natural rara vez tiene TODOS sus terminos en un
    // mismo fragmento. BM25 se encarga de subir los que concentran mas y mejores
    // coincidencias.
    return entrecomillados.join(QStringLiteral(" OR "));
}

bool StudiaIndex::esDiscriminante(int frecuenciaDocumental, int totalFragmentos)
{
    if (frecuenciaDocumental <= 0)
        return false;   // no existe en el corpus
    if (totalFragmentos <= 0)
        return true;    // sin estadisticas, no se descarta nada
    // Piso absoluto: en un indice chico el 1% redondea a cero y ningun termino
    // calificaria, dejando al sistema mudo. Con corpus grandes el piso no
    // interviene (1% de 139.000 son 1.390, muy por encima de 50).
    const int tope = qMax(kMinFrecuenteAbsoluto,
                          int(totalFragmentos * kFraccionFrecuente));
    return frecuenciaDocumental < tope;
}

bool StudiaIndex::hayEvidencia(double mejorScore, const QStringList &terminos,
                               int discriminantes,
                               const QStringList &textosCandidatos, double umbral)
{
    if (terminos.isEmpty() || textosCandidatos.isEmpty())
        return false;

    // Regla 0 — una pregunta hecha solo de palabras muy frecuentes ("¿cual es
    // el mejor equipo de trabajo?") no apunta a nada concreto del corpus.
    if (discriminantes <= 0)
        return false;

    // Regla 1 — fuerza de la coincidencia. BM25 suma el aporte de cada termino,
    // asi que una consulta larga puntua mas alto solo por ser larga: se divide
    // por los terminos que realmente discriminan para poder comparar preguntas
    // de distinto largo.
    if (mejorScore / discriminantes > umbral)
        return false;

    // Regla 2 — la coincidencia tiene que apoyarse en mas de una palabra. Una
    // pregunta ajena al corpus suele enganchar por un unico termino comun
    // ("mejor", "equipo"); una del corpus concentra varios. Se compara sin
    // tildes porque el indice tambien ignora los acentos. Con un solo termino
    // en la consulta la regla no aplica: no hay nada que cubrir.
    if (terminos.size() < 2)
        return true;

    const int mirar = qMin(textosCandidatos.size(), kCandidatosCobertura);
    for (int i = 0; i < mirar; ++i) {
        const QString texto = normalizar(textosCandidatos[i]);
        int distintos = 0;
        for (const QString &t : terminos)
            if (texto.contains(normalizar(t)))
                ++distintos;
        if (distintos >= 2)
            return true;
    }
    return false;
}

bool StudiaIndex::abrir(const QString &dbPath, QString *err)
{
    cerrar();
    if (err) err->clear();

    if (dbPath.trimmed().isEmpty()) {
        if (err) *err = QStringLiteral("No hay ruta de índice configurada.");
        return false;
    }
    if (!QFileInfo::exists(dbPath)) {
        if (err) *err = QStringLiteral("No existe el archivo: %1").arg(dbPath);
        return false;
    }

    m_conn = QStringLiteral("studia_%1_%2")
                 .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()))
                 .arg(QDateTime::currentMSecsSinceEpoch());
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_conn);
    db.setDatabaseName(dbPath);
    // Solo lectura: la ingesta corre aparte y no queremos tocar el indice.
    db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
    if (!db.open()) {
        if (err) *err = QStringLiteral("No se pudo abrir: %1").arg(db.lastError().text());
        QSqlDatabase::removeDatabase(m_conn);
        m_conn.clear();
        return false;
    }

    // Verificar que sea un indice de StudIA y no cualquier SQLite.
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("SELECT COUNT(*) FROM fragmentos_fts "
                               "WHERE fragmentos_fts MATCH '\"x\"' LIMIT 1"))) {
        if (err) {
            *err = QStringLiteral("El archivo no tiene el índice de StudIA "
                                  "(¿corriste tools/studia/ingest.py?). Detalle: %1")
                       .arg(q.lastError().text());
        }
        db.close();
        QSqlDatabase::removeDatabase(m_conn);
        m_conn.clear();
        return false;
    }

    m_ruta = dbPath;
    m_abierto = true;
    return true;
}

void StudiaIndex::cerrar()
{
    if (!m_conn.isEmpty()) {
        {
            QSqlDatabase db = QSqlDatabase::database(m_conn, false);
            if (db.isValid() && db.isOpen())
                db.close();
        }
        QSqlDatabase::removeDatabase(m_conn);
        m_conn.clear();
    }
    m_ruta.clear();
    m_abierto = false;
    m_dfCache.clear();
    m_totalFragmentos = -1;
}

QVector<StudiaFragmento> StudiaIndex::buscar(const QString &consulta, int k,
                                             const QString &materia,
                                             int maxPorDoc) const
{
    QVector<StudiaFragmento> salida;
    if (!m_abierto || k <= 0)
        return salida;
    const QStringList terminos = terminosConsulta(consulta);
    const QString fts = consultaFts(consulta);
    if (fts.isEmpty())
        return salida;

    QSqlDatabase db = QSqlDatabase::database(m_conn, false);
    if (!db.isOpen())
        return salida;

    // Se pide de mas para poder aplicar el tope por documento sin quedarnos
    // cortos: si un solo PDF acapara el top, igual llegamos a k con variedad.
    const int limite = qMax(k * 5, 30);
    QString sql = QStringLiteral(
        "SELECT d.id, d.nombre, d.materia, d.anio, d.ruta, f.pagina, "
        "       bm25(fragmentos_fts) AS score, f.texto, f.id "
        "FROM fragmentos_fts "
        "JOIN fragmentos f ON f.id = fragmentos_fts.rowid "
        "JOIN documentos d ON d.id = f.doc_id "
        "WHERE fragmentos_fts MATCH ? ");
    if (!materia.trimmed().isEmpty())
        sql += QStringLiteral("AND d.materia = ? ");
    sql += QStringLiteral("ORDER BY score LIMIT ?");

    QSqlQuery q(db);
    if (!q.prepare(sql))
        return salida;
    q.addBindValue(fts);
    if (!materia.trimmed().isEmpty())
        q.addBindValue(materia.trimmed());
    q.addBindValue(limite);
    if (!q.exec())
        return salida;

    // Se leen todos los candidatos primero: la regla de abstencion mira los
    // mejores en conjunto, no solo el primero.
    QVector<StudiaFragmento> candidatos;
    while (q.next()) {
        StudiaFragmento f;
        f.docId     = q.value(0).toInt();
        f.documento = q.value(1).toString();
        f.materia   = q.value(2).toString();
        f.anio      = q.value(3).toString();
        f.ruta      = q.value(4).toString();
        f.pagina    = q.value(5).toInt();
        f.score     = q.value(6).toDouble();
        f.texto     = q.value(7).toString();
        f.fragId    = q.value(8).toInt();
        candidatos.append(f);
    }
    if (candidatos.isEmpty())
        return salida;

    QStringList textos;
    for (int i = 0; i < qMin(candidatos.size(), kCandidatosCobertura); ++i)
        textos << candidatos[i].texto;

    // Sin evidencia suficiente se devuelve vacio: el controlador lo traduce en
    // la abstencion, sin llegar a molestar al modelo. En el indice auxiliar de
    // bibliografia propia el gate esta desactivado (ver setExigirEvidencia).
    if (m_exigirEvidencia) {
        const int total = totalFragmentos();
        int discriminantes = 0;
        for (const QString &t : terminos)
            if (esDiscriminante(frecuenciaDocumental(t), total))
                ++discriminantes;
        if (!hayEvidencia(candidatos.first().score, terminos, discriminantes,
                          textos, m_umbral))
            return salida;
    }

    QHash<int, int> porDoc;
    for (const StudiaFragmento &f : candidatos) {
        if (salida.size() >= k)
            break;
        if (maxPorDoc > 0 && porDoc.value(f.docId) >= maxPorDoc)
            continue;
        porDoc[f.docId] = porDoc.value(f.docId) + 1;
        salida.append(f);
    }
    return salida;
}

int StudiaIndex::totalFragmentos() const
{
    if (m_totalFragmentos >= 0)
        return m_totalFragmentos;
    m_totalFragmentos = 0;
    if (!m_abierto)
        return 0;
    QSqlDatabase db = QSqlDatabase::database(m_conn, false);
    if (!db.isOpen())
        return 0;
    QSqlQuery q(db);
    if (q.exec(QStringLiteral("SELECT COUNT(*) FROM fragmentos")) && q.next())
        m_totalFragmentos = q.value(0).toInt();
    return m_totalFragmentos;
}

int StudiaIndex::frecuenciaDocumental(const QString &termino) const
{
    const auto it = m_dfCache.constFind(termino);
    if (it != m_dfCache.constEnd())
        return it.value();
    int n = 0;
    if (m_abierto) {
        QSqlDatabase db = QSqlDatabase::database(m_conn, false);
        if (db.isOpen()) {
            QString escapado = termino;
            escapado.replace(QLatin1Char('"'), QStringLiteral("\"\""));
            QSqlQuery q(db);
            q.prepare(QStringLiteral("SELECT COUNT(*) FROM fragmentos_fts "
                                     "WHERE fragmentos_fts MATCH ?"));
            q.addBindValue(QStringLiteral("\"%1\"").arg(escapado));
            if (q.exec() && q.next())
                n = q.value(0).toInt();
        }
    }
    m_dfCache.insert(termino, n);
    return n;
}

QVariantMap StudiaIndex::estadisticas(const QString &materia) const
{
    QVariantMap m;
    if (!m_abierto)
        return m;
    QSqlDatabase db = QSqlDatabase::database(m_conn, false);
    if (!db.isOpen())
        return m;

    const QString filtro = materia.trimmed();
    const bool acotada = !filtro.isEmpty();
    // Sufijo de WHERE reutilizado por todas las consultas de abajo.
    const QString whereMat = acotada ? QStringLiteral(" WHERE materia=?") : QString();
    auto correr = [&](const QString &sql) {
        auto q = std::make_unique<QSqlQuery>(db);
        if (!q->prepare(sql))
            return std::unique_ptr<QSqlQuery>{};
        if (acotada)
            q->addBindValue(filtro);
        if (!q->exec())
            return std::unique_ptr<QSqlQuery>{};
        return q;
    };

    QVariantMap porEstado;
    int totalDocs = 0;
    if (auto q = correr(QStringLiteral("SELECT estado, COUNT(*) FROM documentos%1 "
                                       "GROUP BY estado").arg(whereMat))) {
        while (q->next()) {
            porEstado.insert(q->value(0).toString(), q->value(1).toInt());
            totalDocs += q->value(1).toInt();
        }
    }
    m.insert(QStringLiteral("porEstado"), porEstado);
    m.insert(QStringLiteral("documentos"), totalDocs);
    m.insert(QStringLiteral("indexados"), porEstado.value(QStringLiteral("ok")).toInt());

    if (auto q = correr(acotada
            ? QStringLiteral("SELECT COUNT(*) FROM fragmentos f "
                             "JOIN documentos d ON d.id=f.doc_id WHERE d.materia=?")
            : QStringLiteral("SELECT COUNT(*) FROM fragmentos")); q && q->next())
        m.insert(QStringLiteral("fragmentos"), q->value(0).toInt());

    if (auto q = correr(QStringLiteral("SELECT COALESCE(SUM(paginas),0) FROM documentos "
                                       "WHERE estado='ok'%1")
                            .arg(acotada ? QStringLiteral(" AND materia=?") : QString()));
        q && q->next())
        m.insert(QStringLiteral("paginas"), q->value(0).toInt());

    // La cantidad de materias sólo tiene sentido en el total.
    if (!acotada) {
        QSqlQuery q(db);
        if (q.exec(QStringLiteral("SELECT COUNT(DISTINCT materia) FROM documentos "
                                  "WHERE estado='ok'")) && q.next())
            m.insert(QStringLiteral("materias"), q.value(0).toInt());
    }
    m.insert(QStringLiteral("materia"), filtro);
    m.insert(QStringLiteral("ruta"), m_ruta);
    return m;
}

// ── Vectores ─────────────────────────────────────────────────────────────────

int StudiaIndex::cantidadVectores() const
{
    if (!m_abierto)
        return 0;
    QSqlDatabase db = QSqlDatabase::database(m_conn, false);
    if (!db.isOpen())
        return 0;
    QSqlQuery q(db);
    // La tabla puede no existir: el indice se pudo generar sin vectorizar.
    if (q.exec(QStringLiteral("SELECT COUNT(*) FROM vectores")) && q.next())
        return q.value(0).toInt();
    return 0;
}

int StudiaIndex::dimensionVectores() const
{
    if (!m_abierto)
        return 0;
    QSqlDatabase db = QSqlDatabase::database(m_conn, false);
    if (!db.isOpen())
        return 0;
    QSqlQuery q(db);
    if (q.exec(QStringLiteral("SELECT valor FROM vectores_info WHERE clave='dim'"))
        && q.next())
        return q.value(0).toInt();
    return 0;
}

bool StudiaIndex::tieneVectores() const
{
    return cantidadVectores() > 0;
}

QVector<StudiaFragmento> StudiaIndex::buscarHibrido(const QString &consulta,
                                                    const QVector<float> &vectorConsulta,
                                                    int k, const QString &materia,
                                                    int maxPorDoc) const
{
    // Sin vector de consulta o sin vectores en el indice: lexico puro.
    if (vectorConsulta.isEmpty() || !tieneVectores())
        return buscar(consulta, k, materia, maxPorDoc);
    if (!m_abierto || k <= 0)
        return {};

    QSqlDatabase db = QSqlDatabase::database(m_conn, false);
    if (!db.isOpen())
        return {};

    // ── Ranking 1: lexico. Se pide de mas para tener con que fusionar.
    const QVector<StudiaFragmento> lexicos =
        buscar(consulta, kCandidatosPorRanking, materia, 0);

    // ── Ranking 2: semantico. Se recorren los vectores y se rankea por coseno.
    // Estan normalizados al guardarlos, asi que el coseno es el producto escalar.
    QString sqlVec = QStringLiteral(
        "SELECT f.id, v.dim, v.vec FROM vectores v "
        "JOIN fragmentos f ON f.id = v.frag_id "
        "JOIN documentos d ON d.id = f.doc_id");
    if (!materia.trimmed().isEmpty())
        sqlVec += QStringLiteral(" WHERE d.materia = ?");
    QSqlQuery qv(db);
    if (!qv.prepare(sqlVec))
        return buscar(consulta, k, materia, maxPorDoc);
    if (!materia.trimmed().isEmpty())
        qv.addBindValue(materia.trimmed());
    if (!qv.exec())
        return buscar(consulta, k, materia, maxPorDoc);

    const int dim = vectorConsulta.size();
    QVector<QPair<float, int>> porSimilitud;   // (coseno, fragmento_id)
    while (qv.next()) {
        if (qv.value(1).toInt() != dim)
            continue;                          // vector de otro modelo: se ignora
        const QByteArray blob = qv.value(2).toByteArray();
        if (blob.size() != dim * int(sizeof(float)))
            continue;
        const float *v = reinterpret_cast<const float *>(blob.constData());
        float dot = 0.f;
        for (int i = 0; i < dim; ++i)
            dot += vectorConsulta[i] * v[i];
        porSimilitud.append({dot, qv.value(0).toInt()});
    }
    if (porSimilitud.isEmpty())
        return buscar(consulta, k, materia, maxPorDoc);
    std::sort(porSimilitud.begin(), porSimilitud.end(),
              [](const auto &a, const auto &b) { return a.first > b.first; });
    if (porSimilitud.size() > kCandidatosPorRanking)
        porSimilitud.resize(kCandidatosPorRanking);

    // ── Fusion RRF: cada lista aporta 1/(k0 + posicion).
    QHash<int, double> puntos;                 // fragmento_id → score fusionado
    for (int i = 0; i < lexicos.size(); ++i)
        puntos[lexicos[i].fragId] += 1.0 / (kRrfK + i + 1);
    for (int i = 0; i < porSimilitud.size(); ++i)
        puntos[porSimilitud[i].second] += 1.0 / (kRrfK + i + 1);

    QVector<int> orden;
    orden.reserve(puntos.size());
    for (auto it = puntos.cbegin(); it != puntos.cend(); ++it)
        orden.append(it.key());
    std::sort(orden.begin(), orden.end(),
              [&](int a, int b) { return puntos[a] > puntos[b]; });

    // ── Materializar los top-k respetando el tope por documento.
    QHash<int, StudiaFragmento> yaTraidos;
    for (const StudiaFragmento &f : lexicos)
        yaTraidos.insert(f.fragId, f);

    QVector<StudiaFragmento> salida;
    QHash<int, int> porDoc;
    for (int fragId : orden) {
        if (salida.size() >= k)
            break;
        StudiaFragmento f;
        if (yaTraidos.contains(fragId)) {
            f = yaTraidos.value(fragId);
        } else {
            // Vino sólo del ranking semántico: hay que leerlo.
            QSqlQuery qf(db);
            qf.prepare(QStringLiteral(
                "SELECT d.id, d.nombre, d.materia, d.anio, d.ruta, f.pagina, f.texto "
                "FROM fragmentos f JOIN documentos d ON d.id = f.doc_id WHERE f.id = ?"));
            qf.addBindValue(fragId);
            if (!qf.exec() || !qf.next())
                continue;
            f.fragId    = fragId;
            f.docId     = qf.value(0).toInt();
            f.documento = qf.value(1).toString();
            f.materia   = qf.value(2).toString();
            f.anio      = qf.value(3).toString();
            f.ruta      = qf.value(4).toString();
            f.pagina    = qf.value(5).toInt();
            f.texto     = qf.value(6).toString();
        }
        if (maxPorDoc > 0 && porDoc.value(f.docId) >= maxPorDoc)
            continue;
        porDoc[f.docId] = porDoc.value(f.docId) + 1;
        f.score = -puntos.value(fragId);   // negativo = mejor, como bm25
        salida.append(f);
    }
    return salida;
}

QVariantList StudiaIndex::documentos(const QString &materia) const
{
    QVariantList out;
    if (!m_abierto)
        return out;
    QSqlDatabase db = QSqlDatabase::database(m_conn, false);
    if (!db.isOpen())
        return out;
    const QString filtro = materia.trimmed();
    QSqlQuery q(db);
    QString sql = QStringLiteral(
        "SELECT nombre, ruta, materia, estado, paginas, fragmentos, detalle "
        "FROM documentos");
    if (!filtro.isEmpty())
        sql += QStringLiteral(" WHERE materia=?");
    sql += QStringLiteral(" ORDER BY ingestado_en DESC, nombre");
    if (!q.prepare(sql))
        return out;
    if (!filtro.isEmpty())
        q.addBindValue(filtro);
    if (!q.exec())
        return out;
    while (q.next()) {
        out.append(QVariantMap{
            {QStringLiteral("nombre"),     q.value(0).toString()},
            {QStringLiteral("ruta"),       q.value(1).toString()},
            {QStringLiteral("materia"),    q.value(2).toString()},
            {QStringLiteral("estado"),     q.value(3).toString()},
            {QStringLiteral("paginas"),    q.value(4).toInt()},
            {QStringLiteral("fragmentos"), q.value(5).toInt()},
            {QStringLiteral("detalle"),    q.value(6).toString()},
        });
    }
    return out;
}

QStringList StudiaIndex::materias() const
{
    QStringList out;
    if (!m_abierto)
        return out;
    QSqlDatabase db = QSqlDatabase::database(m_conn, false);
    if (!db.isOpen())
        return out;
    // Orden de cursada, no alfabetico: por cuatrimestre NUMERICO. El campo
    // `cuatri` es texto ("1C".."10C"), asi que ordenar como string pondria
    // "10C" antes que "1C"; hay que sacarle la letra y castear a entero.
    QSqlQuery q(db);
    if (q.exec(QStringLiteral(
            "SELECT materia, "
            "       MIN(CAST(REPLACE(REPLACE(cuatri,'C',''),'c','') AS INTEGER)) AS orden "
            "FROM documentos "
            "WHERE estado='ok' AND materia<>'' "
            "GROUP BY materia "
            "ORDER BY orden, materia"))) {
        while (q.next())
            out << q.value(0).toString();
    }
    return out;
}
