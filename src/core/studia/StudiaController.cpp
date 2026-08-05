#include "StudiaController.h"

#include <QApplication>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QHash>
#include <QSettings>
#include <QUrl>
#include <QWidget>
#include <algorithm>

namespace {

// Clave de QSettings donde se recuerda el indice elegido.
const char *kClaveRuta = "studia/rutaIndice";

// Cuanto texto de cada fragmento se manda al modelo. Los fragmentos son de
// ~1200 caracteres; con 6 fragmentos son ~7 KB ≈ 1800 tokens, holgado dentro
// de los 32k de contexto del perfil Qwen y deja lugar para la respuesta.
constexpr int kMaxCharsFragmento = 1400;

}  // namespace


StudiaController::StudiaController(QObject *parent) : QObject(parent)
{
    m_nam = new QNetworkAccessManager(this);
    // Reabrir el ultimo indice usado, si sigue existiendo.
    const QString guardada = rutaGuardada();
    if (!guardada.isEmpty() && QFileInfo::exists(guardada))
        abrirIndice(guardada);
}

StudiaController::~StudiaController() { detener(); }

QString StudiaController::fraseAbstencion()
{
    return QStringLiteral("No encontré información suficiente en la documentación "
                          "disponible para responder con seguridad.");
}

// ── Indice ───────────────────────────────────────────────────────────────────

QString StudiaController::rutaGuardada() const
{
    return QSettings().value(QLatin1String(kClaveRuta)).toString();
}

bool StudiaController::abrirIndice(const QString &dbPath)
{
    const bool ok = m_index.abrir(dbPath, &m_errorIndice);
    if (ok) {
        m_errorIndice.clear();
        QSettings().setValue(QLatin1String(kClaveRuta), dbPath);
        // Si el indice cambio, el filtro de materia anterior puede no existir.
        if (!m_materiaFiltro.isEmpty() && !m_index.materias().contains(m_materiaFiltro)) {
            m_materiaFiltro.clear();
            emit materiaFiltroChanged();
        }
    }
    emit indiceChanged();
    return ok;
}

QString StudiaController::elegirIndice()
{
    QWidget *padre = QApplication::activeWindow();
    const QString f = QFileDialog::getOpenFileName(
        padre, QStringLiteral("Elegí el índice de StudIA"),
        QFileInfo(rutaGuardada()).absolutePath(),
        QStringLiteral("Índice de StudIA (*.db);;Todos los archivos (*)"));
    return f;
}

void StudiaController::setMateriaFiltro(const QString &m)
{
    if (m == m_materiaFiltro) return;
    m_materiaFiltro = m;
    emit materiaFiltroChanged();
}

void StudiaController::setServerUrl(const QString &u)
{
    if (u == m_serverUrl) return;
    m_serverUrl = u;
    emit serverUrlChanged();
}

void StudiaController::setModelo(const QString &m)
{
    if (m == m_modelo) return;
    m_modelo = m;
    emit serverUrlChanged();
}

void StudiaController::setFragmentosK(int k)
{
    const int v = qBound(2, k, 15);
    if (v == m_k) return;
    m_k = v;
    emit fragmentosKChanged();
}

void StudiaController::setUmbralAbstencion(double u)
{
    if (qFuzzyCompare(u, m_index.umbralAbstencion())) return;
    m_index.setUmbralAbstencion(u);
    emit fragmentosKChanged();
}

bool StudiaController::abrirDocumento(const QString &ruta) const
{
    if (ruta.isEmpty() || !QFileInfo::exists(ruta))
        return false;
    return QDesktopServices::openUrl(QUrl::fromLocalFile(ruta));
}

QVariantList StudiaController::buscar(const QString &consulta, int k) const
{
    QVariantList out;
    for (const StudiaFragmento &f : m_index.buscar(consulta, k, m_materiaFiltro)) {
        out.append(QVariantMap{
            {QStringLiteral("documento"), f.documento},
            {QStringLiteral("materia"),   f.materia},
            {QStringLiteral("anio"),      f.anio},
            {QStringLiteral("pagina"),    f.pagina},
            {QStringLiteral("score"),     f.score},
            {QStringLiteral("ruta"),      f.ruta},
            {QStringLiteral("texto"),     f.texto},
        });
    }
    return out;
}

// ── Prompt ───────────────────────────────────────────────────────────────────

QString StudiaController::promptSistema()
{
    return QStringLiteral(
        "Sos StudIA, un asistente de estudio para estudiantes de Ingeniería "
        "Mecatrónica de la Universidad Nacional de Lomas de Zamora.\n\n"
        "Respondés APOYÁNDOTE ÚNICAMENTE en los fragmentos de documentación "
        "académica que te paso en cada consulta. Esos fragmentos salen de los "
        "apuntes, libros y trabajos prácticos de la carrera.\n\n"
        "REGLAS:\n"
        "1. Si los fragmentos no alcanzan para responder con seguridad, "
        "respondé exactamente esta frase y nada más: \"%1\"\n"
        "2. No completes con conocimiento general lo que no esté en los "
        "fragmentos. Si sabés algo por tu cuenta pero no está en la "
        "documentación, no lo afirmes.\n"
        "3. Citá siempre de dónde sacaste cada cosa con la referencia [n] del "
        "fragmento correspondiente.\n"
        "4. Explicá de forma clara y progresiva: primero la idea general, "
        "después el detalle técnico. Escribís para alguien que está estudiando "
        "el tema, no para un experto.\n"
        "5. Los fragmentos vienen de PDFs y algunas fórmulas pueden estar mal "
        "transcriptas (símbolos cambiados). Si una fórmula se ve corrupta, "
        "decilo y remití al documento original en vez de reconstruirla.\n"
        "6. Respondé en español rioplatense, en el mismo registro de la "
        "pregunta. Sin relleno ni cortesías innecesarias.")
        .arg(fraseAbstencion());
}

QString StudiaController::construirPrompt(const QString &pregunta,
                                          const QVector<StudiaFragmento> &frags)
{
    QString out = QStringLiteral("### Fragmentos de la documentación académica\n\n");
    for (int i = 0; i < frags.size(); ++i) {
        const StudiaFragmento &f = frags[i];
        out += QStringLiteral("[%1] %2 · %3").arg(i + 1).arg(f.materia, f.documento);
        if (f.pagina > 0)
            out += QStringLiteral(" · pág. %1").arg(f.pagina);
        out += QLatin1Char('\n');
        out += f.texto.left(kMaxCharsFragmento).trimmed();
        out += QStringLiteral("\n\n");
    }
    out += QStringLiteral("### Pregunta del estudiante\n%1\n").arg(pregunta.trimmed());
    return out;
}

QVariantList StudiaController::agruparFuentes(const QVector<StudiaFragmento> &frags)
{
    // Se conserva el orden de aparicion: el documento del fragmento [1] va
    // primero. La clave es la ruta (dos materias pueden tener archivos con el
    // mismo nombre).
    QStringList orden;
    QHash<QString, QVariantMap> datos;
    QHash<QString, QList<int>> refs;      // [n] del prompt
    QHash<QString, QList<int>> paginas;

    for (int i = 0; i < frags.size(); ++i) {
        const StudiaFragmento &f = frags[i];
        const QString clave = f.ruta.isEmpty() ? f.documento : f.ruta;
        if (!datos.contains(clave)) {
            orden << clave;
            datos.insert(clave, QVariantMap{
                {QStringLiteral("documento"), f.documento},
                {QStringLiteral("materia"),   f.materia},
                {QStringLiteral("ruta"),      f.ruta},
            });
        }
        refs[clave].append(i + 1);
        if (f.pagina > 0 && !paginas[clave].contains(f.pagina))
            paginas[clave].append(f.pagina);
    }

    auto unir = [](QList<int> nums) {
        std::sort(nums.begin(), nums.end());
        QStringList s;
        for (int n : nums) s << QString::number(n);
        return s.join(QStringLiteral(", "));
    };

    QVariantList salida;
    for (const QString &clave : orden) {
        QVariantMap m = datos.value(clave);
        m.insert(QStringLiteral("refs"), unir(refs.value(clave)));
        m.insert(QStringLiteral("paginas"), unir(paginas.value(clave)));
        salida.append(m);
    }
    return salida;
}

// ── Conversacion ─────────────────────────────────────────────────────────────

void StudiaController::agregarMensaje(const QString &rol, const QString &contenido,
                                      const QVariantList &fuentes, bool escribiendo)
{
    m_mensajes.append(QVariantMap{
        {QStringLiteral("rol"), rol},
        {QStringLiteral("contenido"), contenido},
        {QStringLiteral("escribiendo"), escribiendo},
        {QStringLiteral("fuentes"), fuentes},
    });
    emit mensajesChanged();
}

void StudiaController::limpiar()
{
    detener();
    m_mensajes.clear();
    m_idxRespuesta = -1;
    emit mensajesChanged();
}

void StudiaController::preguntar(const QString &texto)
{
    const QString pregunta = texto.trimmed();
    if (pregunta.isEmpty() || m_reply)
        return;

    agregarMensaje(QStringLiteral("usuario"), pregunta);

    if (!m_index.abierto()) {
        agregarMensaje(QStringLiteral("asistente"),
                       QStringLiteral("No hay un índice documental abierto. "
                                      "Elegí el archivo del índice para empezar."));
        return;
    }

    const QVector<StudiaFragmento> frags =
        m_index.buscar(pregunta, m_k, m_materiaFiltro);

    // Sin evidencia no se consulta al modelo: se responde la abstencion. Asi la
    // garantia no depende de que el modelo obedezca el prompt.
    if (frags.isEmpty()) {
        agregarMensaje(QStringLiteral("asistente"), fraseAbstencion());
        return;
    }

    agregarMensaje(QStringLiteral("asistente"), QString(), agruparFuentes(frags), true);
    m_idxRespuesta = m_mensajes.size() - 1;
    generar(construirPrompt(pregunta, frags));
}

void StudiaController::generar(const QString &promptUsuario)
{
    if (m_serverUrl.trimmed().isEmpty()) {
        cerrarStream(false, QStringLiteral("El servidor no está activo. "
                                           "Arrancalo desde la sección Lanzar."));
        return;
    }

    QJsonArray msgs;
    msgs.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                            {QStringLiteral("content"), promptSistema()}});
    msgs.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                            {QStringLiteral("content"), promptUsuario}});

    QJsonObject payload{
        {QStringLiteral("model"), m_modelo},
        {QStringLiteral("messages"), msgs},
        {QStringLiteral("stream"), true},
        // Temperatura baja: la tarea es resumir documentación, no inventar.
        {QStringLiteral("temperature"), 0.3},
        // Sin razonamiento interno: queremos la respuesta directa.
        {QStringLiteral("reasoning_budget"), 0},
        {QStringLiteral("chat_template_kwargs"),
         QJsonObject{{QStringLiteral("enable_thinking"), false}}},
    };

    QNetworkRequest req(QUrl(m_serverUrl + QStringLiteral("/v1/chat/completions")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));

    m_sseBuf.clear();
    m_acumulado.clear();
    m_reply = m_nam->post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    emit generandoChanged();

    connect(m_reply, &QNetworkReply::readyRead, this, [this]() {
        if (!m_reply) return;
        m_sseBuf.append(m_reply->readAll());
        while (true) {
            const int nl = m_sseBuf.indexOf('\n');
            if (nl < 0) break;
            const QByteArray linea = m_sseBuf.left(nl).trimmed();
            m_sseBuf.remove(0, nl + 1);
            if (!linea.startsWith("data: ")) continue;
            const QByteArray data = linea.mid(6).trimmed();
            if (data == "[DONE]") continue;
            const QJsonDocument d = QJsonDocument::fromJson(data);
            if (!d.isObject()) continue;
            const QJsonArray choices = d.object().value(QStringLiteral("choices")).toArray();
            if (choices.isEmpty()) continue;
            const QString trozo = choices.first().toObject()
                                      .value(QStringLiteral("delta")).toObject()
                                      .value(QStringLiteral("content")).toString();
            if (trozo.isEmpty()) continue;
            m_acumulado += trozo;
            if (m_idxRespuesta >= 0 && m_idxRespuesta < m_mensajes.size()) {
                QVariantMap msg = m_mensajes[m_idxRespuesta].toMap();
                msg[QStringLiteral("contenido")] = m_acumulado;
                m_mensajes[m_idxRespuesta] = msg;
                emit textoParcial(m_idxRespuesta, m_acumulado);
            }
        }
    });

    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        if (!m_reply) return;
        const bool ok = m_reply->error() == QNetworkReply::NoError;
        QString err = ok ? QString() : m_reply->errorString();
        if (!ok) {
            const QString cuerpo = QString::fromUtf8(m_reply->readAll()).trimmed();
            if (!cuerpo.isEmpty())
                err += QStringLiteral(" · %1").arg(cuerpo.left(500));
        }
        cerrarStream(ok, err);
    });
}

void StudiaController::cerrarStream(bool ok, const QString &err)
{
    if (m_reply) {
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    if (m_idxRespuesta >= 0 && m_idxRespuesta < m_mensajes.size()) {
        QVariantMap msg = m_mensajes[m_idxRespuesta].toMap();
        msg[QStringLiteral("escribiendo")] = false;
        if (!ok && m_acumulado.isEmpty()) {
            msg[QStringLiteral("contenido")] = QStringLiteral("[error: %1]").arg(err);
            msg[QStringLiteral("fuentes")] = QVariantList{};
        }
        m_mensajes[m_idxRespuesta] = msg;
    }
    m_idxRespuesta = -1;
    m_acumulado.clear();
    m_sseBuf.clear();
    emit mensajesChanged();
    emit generandoChanged();
    if (!ok && !err.isEmpty())
        emit errorOcurrido(err);
}

void StudiaController::detener()
{
    if (!m_reply)
        return;
    QNetworkReply *r = m_reply;
    m_reply = nullptr;
    r->abort();
    r->deleteLater();
    if (m_idxRespuesta >= 0 && m_idxRespuesta < m_mensajes.size()) {
        QVariantMap msg = m_mensajes[m_idxRespuesta].toMap();
        msg[QStringLiteral("escribiendo")] = false;
        m_mensajes[m_idxRespuesta] = msg;
    }
    m_idxRespuesta = -1;
    m_acumulado.clear();
    m_sseBuf.clear();
    emit mensajesChanged();
    emit generandoChanged();
}
