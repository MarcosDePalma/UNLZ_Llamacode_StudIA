#include <QtTest>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "core/studia/StudiaController.h"
#include "core/studia/StudiaEmbed.h"
#include "core/studia/StudiaHerramientas.h"
#include "core/studia/StudiaIndex.h"
#include "core/studia/StudiaPrompt.h"
#include "core/studia/StudiaSessionStore.h"
#include "core/studia/StudiaTexto.h"

// Cubre el modulo StudIA sin red ni servidor: la traduccion de preguntas a
// consultas FTS5, la busqueda sobre un indice sintetico y el armado del prompt.
class TestStudia : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QString m_db;

    // Crea un indice con el mismo esquema que genera tools/studia/ingest.py.
    bool construirIndice(const QString &path)
    {
        const QString conn = QStringLiteral("studia_test_build");
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
            db.setDatabaseName(path);
            if (!db.open())
                return false;
            QSqlQuery q(db);
            const char *ddl[] = {
                "CREATE TABLE documentos (id INTEGER PRIMARY KEY, ruta TEXT UNIQUE, "
                "nombre TEXT, anio TEXT, cuatri TEXT, materia_cod TEXT, materia TEXT, "
                "subruta TEXT, ext TEXT, bytes INTEGER, huella TEXT, paginas INTEGER, "
                "chars INTEGER, fragmentos INTEGER, estado TEXT, detalle TEXT, "
                "ingestado_en TEXT)",
                "CREATE TABLE fragmentos (id INTEGER PRIMARY KEY, doc_id INTEGER, "
                "pagina INTEGER, orden INTEGER, chars INTEGER, texto TEXT)",
                "CREATE VIRTUAL TABLE fragmentos_fts USING fts5(texto, "
                "content='fragmentos', content_rowid='id', "
                "tokenize=\"unicode61 remove_diacritics 2\")",
                // De dónde se ingestó: con esto la app reubica los documentos
                // cuando el corpus viaja con la aplicación.
                "CREATE TABLE indice_info (clave TEXT PRIMARY KEY, valor TEXT)",
                "INSERT INTO indice_info VALUES ('corpus_raiz','C:/corpus')",
            };
            for (const char *s : ddl)
                if (!q.exec(QLatin1String(s)))
                    return false;

            auto doc = [&](int id, const QString &nombre, const QString &materia,
                           const QString &anio, const QString &cuatri, int paginas,
                           const QString &estado) {
                QSqlQuery i(db);
                i.prepare(QStringLiteral(
                    "INSERT INTO documentos (id,ruta,nombre,anio,cuatri,materia,paginas,estado) "
                    "VALUES (?,?,?,?,?,?,?,?)"));
                i.addBindValue(id);
                i.addBindValue(QStringLiteral("C:/corpus/%1").arg(nombre));
                i.addBindValue(nombre);
                i.addBindValue(anio);
                i.addBindValue(cuatri);
                i.addBindValue(materia);
                i.addBindValue(paginas);
                i.addBindValue(estado);
                return i.exec();
            };
            auto frag = [&](int id, int docId, int pagina, const QString &texto) {
                QSqlQuery i(db);
                i.prepare(QStringLiteral("INSERT INTO fragmentos "
                                         "(id,doc_id,pagina,orden,chars,texto) "
                                         "VALUES (?,?,?,0,?,?)"));
                i.addBindValue(id);
                i.addBindValue(docId);
                i.addBindValue(pagina);
                i.addBindValue(texto.size());
                i.addBindValue(texto);
                if (!i.exec())
                    return false;
                QSqlQuery f(db);
                f.prepare(QStringLiteral("INSERT INTO fragmentos_fts (rowid,texto) VALUES (?,?)"));
                f.addBindValue(id);
                f.addBindValue(texto);
                return f.exec();
            };

            // Cuatrimestres elegidos a proposito para que el orden de cursada
            // (9 antes que 10) NO coincida ni con el alfabetico ni con el
            // orden de texto de "cuatri" ("10C" < "9C" como string).
            if (!doc(1, QStringLiteral("ogata.pdf"), QStringLiteral("Sistemas de Control"),
                     QStringLiteral("5A"), QStringLiteral("9C"), 900, QStringLiteral("ok")))
                return false;
            if (!doc(2, QStringLiteral("modbus.pdf"), QStringLiteral("Redes Industriales"),
                     QStringLiteral("5A"), QStringLiteral("10C"), 40, QStringLiteral("ok")))
                return false;
            if (!doc(3, QStringLiteral("escaneado.pdf"), QStringLiteral("Termodinámica"),
                     QStringLiteral("3A"), QStringLiteral("5C"), 12,
                     QStringLiteral("necesita_ocr")))
                return false;

            // Cuatro fragmentos del mismo documento: sirven para verificar el
            // tope por documento (maxPorDoc).
            if (!frag(1, 1, 266, QStringLiteral("El criterio de estabilidad de Routh permite "
                                                "determinar la cantidad de polos inestables.")))
                return false;
            if (!frag(2, 1, 267, QStringLiteral("La tabla de Routh se construye con los "
                                                "coeficientes del polinomio característico.")))
                return false;
            if (!frag(3, 1, 268, QStringLiteral("El criterio de Hurwitz es equivalente al "
                                                "criterio de Routh para la estabilidad.")))
                return false;
            if (!frag(4, 1, 269, QStringLiteral("Un sistema es estable si todos los polos "
                                                "tienen parte real negativa segun Routh.")))
                return false;
            if (!frag(5, 2, 3, QStringLiteral("Modbus TCP encapsula la trama Modbus sobre "
                                              "una red Ethernet conmutada.")))
                return false;
            // Comparte vocabulario con los fragmentos de Sistemas de Control
            // ("estabilidad"): permite comprobar que el filtro por materia
            // realmente descarta la otra materia y no que simplemente no hubo
            // coincidencias.
            if (!frag(6, 2, 4, QStringLiteral("La estabilidad de una red Modbus depende del "
                                              "cableado y de la terminación del bus.")))
                return false;
            db.close();
        }
        QSqlDatabase::removeDatabase(conn);
        return true;
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        // setTestModeEnabled redirige carpetas, no el registro de Windows: con
        // el formato nativo, un QSettings del test escribe en HKCU igual que la
        // app. Forzando el formato .ini las preferencias caen dentro de la
        // carpeta de test y no se toca nada del usuario. Es la misma clase de
        // fuga que llenaba de perfiles duplicados la carpeta Documentos.
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QVERIFY(m_dir.isValid());
        m_db = m_dir.filePath(QStringLiteral("studia_test.db"));
        QVERIFY2(construirIndice(m_db), "no se pudo construir el indice de prueba");
    }

    // Las sesiones se persisten en UN archivo compartido, asi que sin esto un
    // test hereda los temas que dejo el anterior y las aserciones dependen del
    // orden en que corren.
    void init()
    {
        QFile::remove(StudiaSessionStore().rutaArchivo());
    }

    // ── consultaFts: pregunta natural → expresion FTS5 ──

    void consultaFts_quitaPalabrasVacias()
    {
        const QString q = StudiaIndex::consultaFts(
            QStringLiteral("¿Qué es el criterio de estabilidad?"));
        QVERIFY(q.contains(QStringLiteral("\"criterio\"")));
        QVERIFY(q.contains(QStringLiteral("\"estabilidad\"")));
        QVERIFY(!q.contains(QStringLiteral("\"que\"")));
        QVERIFY(!q.contains(QStringLiteral("\"el\"")));
        QVERIFY(!q.contains(QStringLiteral("\"es\"")));
    }

    void consultaFts_uneConOr()
    {
        const QString q = StudiaIndex::consultaFts(QStringLiteral("modbus ethernet"));
        QCOMPARE(q, QStringLiteral("\"modbus\" OR \"ethernet\""));
    }

    void consultaFts_descartaTerminosCortos()
    {
        const QString q = StudiaIndex::consultaFts(QStringLiteral("rs 485 de un plc"));
        QVERIFY(!q.contains(QStringLiteral("\"rs\"")));   // 2 letras
        QVERIFY(q.contains(QStringLiteral("\"485\"")));
        QVERIFY(q.contains(QStringLiteral("\"plc\"")));
    }

    void consultaFts_deduplica()
    {
        // "Control" y "control" son el mismo termino; no debe repetirse.
        const QString q = StudiaIndex::consultaFts(QStringLiteral("Control control CONTROL"));
        QCOMPARE(q, QStringLiteral("\"control\""));
    }

    void consultaFts_ignoraTildesAlFiltrar()
    {
        // "qué" con tilde tambien es palabra vacia.
        QVERIFY(StudiaIndex::esPalabraVacia(QStringLiteral("qué")));
        QVERIFY(StudiaIndex::esPalabraVacia(QStringLiteral("que")));
        QVERIFY(!StudiaIndex::esPalabraVacia(QStringLiteral("modbus")));
    }

    void consultaFts_escapaComillas()
    {
        const QString q = StudiaIndex::consultaFts(QStringLiteral("norma \"IEC\" 61131"));
        // La comilla interna se duplica; la expresion sigue siendo valida.
        QVERIFY(!q.isEmpty());
        QVERIFY(q.contains(QStringLiteral("\"iec\"")));
    }

    void consultaFts_vaciaSiTodoEsRuido()
    {
        QVERIFY(StudiaIndex::consultaFts(QStringLiteral("y el de la o un")).isEmpty());
        QVERIFY(StudiaIndex::consultaFts(QStringLiteral("   ")).isEmpty());
        QVERIFY(StudiaIndex::consultaFts(QStringLiteral("?? !!")).isEmpty());
    }

    // ── Regla de abstencion (lo que evita responder sin evidencia) ──

    void evidencia_rechazaEntradasVacias()
    {
        QVERIFY(!StudiaIndex::hayEvidencia(-50.0, {}, 1, {QStringLiteral("texto")}, -7.0));
        QVERIFY(!StudiaIndex::hayEvidencia(-50.0, {QStringLiteral("routh")}, 1, {}, -7.0));
    }

    void evidencia_rechazaPreguntaSinTerminosEspecificos()
    {
        // "¿cual es el mejor equipo de trabajo?": todo palabras frecuentes.
        // Sin ningun termino discriminante no hay nada concreto que buscar.
        const QStringList t{QStringLiteral("equipo"), QStringLiteral("trabajo")};
        QVERIFY(!StudiaIndex::hayEvidencia(
            -40.0, t, 0, {QStringLiteral("el equipo de trabajo debe usar casco")}, -7.0));
    }

    void evidencia_rechazaScoreDebil()
    {
        // 2 discriminantes, score -12 → -6.0, peor que el umbral -7.0.
        const QStringList t{QStringLiteral("receta"), QStringLiteral("milanesas"),
                            QStringLiteral("napolitanas")};
        QVERIFY(!StudiaIndex::hayEvidencia(
            -12.0, t, 2, {QStringLiteral("la receta de todas")}, -7.0));
    }

    void evidencia_aceptaScoreFuerte()
    {
        const QStringList t{QStringLiteral("criterio"), QStringLiteral("estabilidad"),
                            QStringLiteral("routh"), QStringLiteral("hurwitz")};
        QVERIFY(StudiaIndex::hayEvidencia(
            -38.0, t, 4, {QStringLiteral("el criterio de Routh determina la estabilidad")}, -7.0));
    }

    void evidencia_elRellenoNoDiluyeElScore()
    {
        // El caso que motivo el cambio: "explicame de que se trata un motor a
        // induccion" deja terminos de relleno que no aportan al ranking. Si el
        // divisor fuera la cantidad TOTAL de terminos, la pregunta se rechaza;
        // dividiendo solo por los discriminantes, pasa.
        const QStringList t{QStringLiteral("motor"), QStringLiteral("induccion"),
                            QStringLiteral("relleno1"), QStringLiteral("relleno2"),
                            QStringLiteral("relleno3")};
        const QStringList texto{QStringLiteral("el motor de induccion tipo jaula de ardilla")};
        QVERIFY(!StudiaIndex::hayEvidencia(-30.0, t, t.size(), texto, -7.0));  // -6.0: rechaza
        QVERIFY(StudiaIndex::hayEvidencia(-30.0, t, 2, texto, -7.0));          // -15.0: acepta
    }

    void evidencia_exigeMasDeUnTerminoEnElFragmento()
    {
        // Score holgado, pero los fragmentos enganchan por UNA sola palabra:
        // es el patron tipico de una pregunta ajena al corpus.
        const QStringList t{QStringLiteral("campeon"), QStringLiteral("libertadores"),
                            QStringLiteral("penales")};
        QVERIFY(!StudiaIndex::hayEvidencia(
            -40.0, t, 2, {QStringLiteral("el equipo de trabajo debe usar casco"),
                          QStringLiteral("penales por incumplimiento del contrato")}, -7.0));
    }

    void evidencia_alcanzaConQueUnCandidatoDelTopeCubra()
    {
        // El mejor fragmento cubre un solo termino, pero el tercero cubre dos.
        const QStringList t{QStringLiteral("routh"), QStringLiteral("estabilidad")};
        const QStringList textos{
            QStringLiteral("tabla de Routh para el polinomio"),          // 1
            QStringLiteral("otro parrafo que menciona Routh"),           // 1
            QStringLiteral("el criterio de Routh define la estabilidad") // 2
        };
        QVERIFY(StudiaIndex::hayEvidencia(-30.0, t, 2, textos, -7.0));
    }

    void evidencia_soloMiraElTopeDeCandidatos()
    {
        const QStringList t{QStringLiteral("routh"), QStringLiteral("estabilidad")};
        QStringList textos;
        for (int i = 0; i < StudiaIndex::kCandidatosCobertura; ++i)
            textos << QStringLiteral("solo menciona Routh");
        textos << QStringLiteral("Routh y estabilidad juntos");
        QVERIFY(!StudiaIndex::hayEvidencia(-30.0, t, 2, textos, -7.0));
    }

    void evidencia_terminoUnicoNoExigeCobertura()
    {
        const QStringList t{QStringLiteral("modbus")};
        QVERIFY(StudiaIndex::hayEvidencia(
            -20.0, t, 1, {QStringLiteral("el protocolo Modbus")}, -7.0));
    }

    void evidencia_ignoraTildesAlContarCobertura()
    {
        const QStringList t{QStringLiteral("mecanica"), QStringLiteral("teorica")};
        QVERIFY(StudiaIndex::hayEvidencia(
            -30.0, t, 2, {QStringLiteral("Mecánica Teórica aplicada")}, -7.0));
    }

    void evidencia_umbralConfigurable()
    {
        const QStringList t{QStringLiteral("criterio"), QStringLiteral("estabilidad")};
        const QStringList texto{QStringLiteral("criterio de estabilidad")};
        // -10.0/2 = -5.0. Con umbral -4 pasa; con umbral -6 no.
        QVERIFY(StudiaIndex::hayEvidencia(-10.0, t, 2, texto, -4.0));
        QVERIFY(!StudiaIndex::hayEvidencia(-10.0, t, 2, texto, -6.0));
    }

    void discriminante_descartaAusentesYMuyFrecuentes()
    {
        // Ausente del corpus.
        QVERIFY(!StudiaIndex::esDiscriminante(0, 100000));
        // Presente pero en el 5% de los fragmentos: no discrimina (tope 1%).
        QVERIFY(!StudiaIndex::esDiscriminante(5000, 100000));
        // Presente en el 0.1%: si discrimina.
        QVERIFY(StudiaIndex::esDiscriminante(100, 100000));
        // Sin estadisticas de corpus no se descarta nada.
        QVERIFY(StudiaIndex::esDiscriminante(3, 0));
    }

    void discriminante_pisoParaIndicesChicos()
    {
        // Con 6 fragmentos, el 1% es 0.06: sin piso, ningun termino calificaria
        // y el asistente se abstendria siempre. El piso absoluto lo evita.
        QVERIFY(StudiaIndex::esDiscriminante(1, 6));
        QVERIFY(StudiaIndex::esDiscriminante(40, 200));
        // Pero el piso no debe volverlo permisivo en corpus grandes.
        QVERIFY(!StudiaIndex::esDiscriminante(2000, 100000));
    }

    void terminosConsulta_descartaMuletillasDePreguntaNatural()
    {
        // El caso reportado: sin esto "trata" contaba como termino tecnico.
        const QStringList t = StudiaIndex::terminosConsulta(
            QStringLiteral("explicame de que se trata un motor a induccion"));
        QCOMPARE(t, QStringList({QStringLiteral("motor"), QStringLiteral("induccion")}));
    }

    void terminosConsulta_muletillasVariadas()
    {
        QCOMPARE(StudiaIndex::terminosConsulta(
                     QStringLiteral("¿para qué sirve un PLC en la industria?")),
                 QStringList({QStringLiteral("plc"), QStringLiteral("industria")}));
        QCOMPARE(StudiaIndex::terminosConsulta(
                     QStringLiteral("contame cómo funciona un transistor bipolar")),
                 QStringList({QStringLiteral("transistor"), QStringLiteral("bipolar")}));
    }

    void buscar_umbralExigenteFuerzaLaAbstencion()
    {
        StudiaIndex idx;
        QVERIFY(idx.abrir(m_db));
        // Con el umbral por defecto la consulta del corpus recupera algo...
        idx.setUmbralAbstencion(0.0);   // permisivo: acepta cualquier score
        QVERIFY(!idx.buscar(QStringLiteral("criterio de estabilidad de Routh"), 5).isEmpty());
        // ...y con un umbral imposible, no.
        idx.setUmbralAbstencion(-1e9);
        QVERIFY(idx.buscar(QStringLiteral("criterio de estabilidad de Routh"), 5).isEmpty());
    }

    void terminosConsulta_devuelveLosTerminosCrudos()
    {
        const QStringList t = StudiaIndex::terminosConsulta(
            QStringLiteral("¿Qué es el criterio de Routh?"));
        QCOMPARE(t, QStringList({QStringLiteral("criterio"), QStringLiteral("routh")}));
    }

    // ── Indice ──

    void indice_abreYFalla()
    {
        StudiaIndex idx;
        QString err;
        QVERIFY(!idx.abrir(QStringLiteral("C:/no/existe/nada.db"), &err));
        QVERIFY(!err.isEmpty());
        QVERIFY(!idx.abierto());

        QVERIFY2(idx.abrir(m_db, &err), qPrintable(err));
        QVERIFY(idx.abierto());
        QCOMPARE(idx.rutaDb(), m_db);
    }

    void indice_rechazaSqliteAjeno()
    {
        // Una base valida pero sin el esquema de StudIA debe fallar con motivo.
        const QString otra = m_dir.filePath(QStringLiteral("ajena.db"));
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                        QStringLiteral("ajena"));
            db.setDatabaseName(otra);
            QVERIFY(db.open());
            QSqlQuery(db).exec(QStringLiteral("CREATE TABLE cosas (x INTEGER)"));
            db.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("ajena"));

        StudiaIndex idx;
        QString err;
        QVERIFY(!idx.abrir(otra, &err));
        QVERIFY(err.contains(QStringLiteral("StudIA")));
    }

    // Los tests de mecanica de busqueda usan umbral permisivo a proposito: los
    // valores de BM25 dependen del tamano del corpus, y este indice sintetico
    // tiene 5 fragmentos, asi que sus scores no son comparables con los del
    // corpus real que calibro el default. La regla de abstencion se prueba
    // aparte, en los tests de hayEvidencia().
    void buscar_encuentraLoRelevante()
    {
        StudiaIndex idx;
        QVERIFY(idx.abrir(m_db));
        idx.setUmbralAbstencion(0.0);
        const auto r = idx.buscar(QStringLiteral("criterio de estabilidad de Routh"), 5);
        QVERIFY(!r.isEmpty());
        QCOMPARE(r.first().documento, QStringLiteral("ogata.pdf"));
        QCOMPARE(r.first().materia, QStringLiteral("Sistemas de Control"));
        QVERIFY(r.first().pagina > 0);
        QVERIFY(!r.first().texto.isEmpty());
    }

    void buscar_sinCoincidenciasDevuelveVacio()
    {
        StudiaIndex idx;
        QVERIFY(idx.abrir(m_db));
        QVERIFY(idx.buscar(QStringLiteral("gastronomía molecular"), 5).isEmpty());
        // Consulta que solo tiene palabras vacias: tampoco debe devolver nada.
        QVERIFY(idx.buscar(QStringLiteral("y el de la"), 5).isEmpty());
    }

    void buscar_filtraPorMateria()
    {
        StudiaIndex idx;
        QVERIFY(idx.abrir(m_db));
        idx.setUmbralAbstencion(0.0);
        // "estabilidad" aparece en las dos materias; el filtro debe dejar solo una.
        const QString consulta = QStringLiteral("estabilidad Modbus red");
        QVERIFY(idx.buscar(consulta, 5).size() > 0);   // sin filtro hay de ambas
        const auto r = idx.buscar(consulta, 5, QStringLiteral("Redes Industriales"));
        QVERIFY(!r.isEmpty());
        for (const auto &f : r)
            QCOMPARE(f.materia, QStringLiteral("Redes Industriales"));
    }

    void buscar_limitaFragmentosPorDocumento()
    {
        StudiaIndex idx;
        QVERIFY(idx.abrir(m_db));
        idx.setUmbralAbstencion(0.0);
        // ogata.pdf tiene 4 fragmentos sobre Routh; con maxPorDoc=2 solo entran 2.
        const auto r = idx.buscar(QStringLiteral("Routh estabilidad polos"), 10,
                                  QString(), 2);
        int deOgata = 0;
        for (const auto &f : r)
            if (f.documento == QLatin1String("ogata.pdf"))
                ++deOgata;
        QCOMPARE(deOgata, 2);
    }

    void buscar_respetaK()
    {
        StudiaIndex idx;
        QVERIFY(idx.abrir(m_db));
        idx.setUmbralAbstencion(0.0);
        QCOMPARE(idx.buscar(QStringLiteral("Routh estabilidad"), 1, QString(), 5).size(), 1);
        QVERIFY(idx.buscar(QStringLiteral("Routh"), 0).isEmpty());
    }

    void buscar_indiceCerradoNoRompe()
    {
        StudiaIndex idx;
        QVERIFY(idx.buscar(QStringLiteral("Routh"), 5).isEmpty());
        QVERIFY(idx.materias().isEmpty());
        QVERIFY(idx.estadisticas().isEmpty());
    }

    void estadisticas_cuentanPorEstado()
    {
        StudiaIndex idx;
        QVERIFY(idx.abrir(m_db));
        const QVariantMap e = idx.estadisticas();
        QCOMPARE(e.value(QStringLiteral("documentos")).toInt(), 3);
        QCOMPARE(e.value(QStringLiteral("indexados")).toInt(), 2);   // ok
        QCOMPARE(e.value(QStringLiteral("fragmentos")).toInt(), 6);
        QCOMPARE(e.value(QStringLiteral("materias")).toInt(), 2);    // solo estado ok
        const QVariantMap porEstado = e.value(QStringLiteral("porEstado")).toMap();
        QCOMPARE(porEstado.value(QStringLiteral("necesita_ocr")).toInt(), 1);
    }

    void materias_soloLasIndexadas()
    {
        StudiaIndex idx;
        QVERIFY(idx.abrir(m_db));
        const QStringList m = idx.materias();
        QCOMPARE(m.size(), 2);
        QVERIFY(m.contains(QStringLiteral("Sistemas de Control")));
        QVERIFY(m.contains(QStringLiteral("Redes Industriales")));
        // La materia del documento sin texto (necesita_ocr) no se ofrece.
        QVERIFY(!m.contains(QStringLiteral("Termodinámica")));
    }

    void estadisticas_acotadasAUnaMateria()
    {
        StudiaIndex idx;
        QVERIFY(idx.abrir(m_db));
        // Sistemas de Control: 1 documento ok (ogata.pdf, 900 páginas) y 4 frags.
        const QVariantMap e = idx.estadisticas(QStringLiteral("Sistemas de Control"));
        QCOMPARE(e.value(QStringLiteral("documentos")).toInt(), 1);
        QCOMPARE(e.value(QStringLiteral("indexados")).toInt(), 1);
        QCOMPARE(e.value(QStringLiteral("fragmentos")).toInt(), 4);
        QCOMPARE(e.value(QStringLiteral("paginas")).toInt(), 900);
        QCOMPARE(e.value(QStringLiteral("materia")).toString(),
                 QStringLiteral("Sistemas de Control"));
        // El total sigue siendo mayor: la de la materia no lo pisa.
        QCOMPARE(idx.estadisticas().value(QStringLiteral("fragmentos")).toInt(), 6);
    }

    void estadisticas_materiaInexistenteDaCero()
    {
        StudiaIndex idx;
        QVERIFY(idx.abrir(m_db));
        const QVariantMap e = idx.estadisticas(QStringLiteral("No Existe"));
        QCOMPARE(e.value(QStringLiteral("documentos")).toInt(), 0);
        QCOMPARE(e.value(QStringLiteral("fragmentos")).toInt(), 0);
    }

    void estadisticas_soloElTotalCuentaMaterias()
    {
        StudiaIndex idx;
        QVERIFY(idx.abrir(m_db));
        // "materias" es un dato del corpus entero; acotado no tiene sentido.
        QVERIFY(idx.estadisticas().contains(QStringLiteral("materias")));
        QVERIFY(!idx.estadisticas(QStringLiteral("Sistemas de Control"))
                     .contains(QStringLiteral("materias")));
    }

    void controlador_estadisticasDeLaMateriaActiva()
    {
        StudiaController c;
        QVERIFY(c.abrirIndice(m_db));
        c.setMateria(QStringLiteral("Redes Industriales"));
        QCOMPARE(c.estadisticasMateria().value(QStringLiteral("fragmentos")).toInt(), 2);
        c.setMateria(QStringLiteral("Sistemas de Control"));
        QCOMPARE(c.estadisticasMateria().value(QStringLiteral("fragmentos")).toInt(), 4);
    }

    void bibliotecaPropia_esUnIndiceSeparado()
    {
        StudiaController c;
        QVERIFY(c.abrirIndice(m_db));
        // Nunca es el mismo archivo que el de la cátedra: lo que sube el
        // estudiante no puede mezclarse con el corpus institucional.
        QVERIFY(!c.rutaIndicePropio().isEmpty());
        QVERIFY(c.rutaIndicePropio() != c.rutaIndice());
        QVERIFY(c.rutaIndicePropio().endsWith(QStringLiteral("mi_biblioteca.db")));
    }

    void bibliotecaPropia_sinMateriaNoAdjunta()
    {
        StudiaController c;
        QVERIFY(c.abrirIndice(m_db));
        c.setMateria(QString());
        QSignalSpy spy(&c, &StudiaController::errorOcurrido);
        c.adjuntarBibliografia({m_db});   // archivo que existe, pero sin materia
        QCOMPARE(spy.count(), 1);
        QVERIFY(!c.adjuntando());
        QCOMPARE(c.adjuntosPendientes(), 0);
    }

    void bibliotecaPropia_archivoInexistenteAvisa()
    {
        StudiaController c;
        QVERIFY(c.abrirIndice(m_db));
        c.setMateria(QStringLiteral("Sistemas de Control"));
        QSignalSpy spy(&c, &StudiaController::errorOcurrido);
        c.adjuntarBibliografia({QStringLiteral("C:/no/existe/apunte.pdf")});
        QCOMPARE(spy.count(), 1);
        QVERIFY(!c.adjuntando());
        QCOMPARE(c.adjuntosPendientes(), 0);
    }

    void bibliotecaPropia_listaVaciaNoHaceNada()
    {
        StudiaController c;
        QVERIFY(c.abrirIndice(m_db));
        c.setMateria(QStringLiteral("Sistemas de Control"));
        QSignalSpy spy(&c, &StudiaController::errorOcurrido);
        c.adjuntarBibliografia({});      // el usuario canceló el diálogo
        QCOMPARE(spy.count(), 0);
        QVERIFY(!c.adjuntando());
    }

    void bibliotecaPropia_variosArchivosSeEncolan()
    {
        StudiaController c;
        QVERIFY(c.abrirIndice(m_db));
        c.setMateria(QStringLiteral("Sistemas de Control"));
        // Tres archivos que existen: uno se procesa, los otros quedan en cola.
        // (Sin Python en el entorno el proceso falla, pero la cola igual se arma.)
        c.adjuntarBibliografia({m_db, m_db, m_db});
        QVERIFY(c.adjuntando() || c.adjuntosPendientes() > 0
                || !c.rutaIndicePropio().isEmpty());
    }

    void bibliotecaPropia_avisaSoloDeLosQueFaltan()
    {
        StudiaController c;
        QVERIFY(c.abrirIndice(m_db));
        c.setMateria(QStringLiteral("Sistemas de Control"));
        QSignalSpy spy(&c, &StudiaController::errorOcurrido);
        // Uno válido y uno inexistente: avisa por el que falta, procesa el otro.
        c.adjuntarBibliografia({m_db, QStringLiteral("C:/no/existe.pdf")});
        QCOMPARE(spy.count(), 1);
        QVERIFY(spy.first().first().toString().contains(QStringLiteral("existe.pdf")));
    }

    void materias_ordenadasPorCuatrimestreDeCursada()
    {
        StudiaIndex idx;
        QVERIFY(idx.abrir(m_db));
        const QStringList m = idx.materias();
        // Sistemas de Control es 9C y Redes es 10C: primero va la que se cursa
        // antes. Ordenar alfabeticamente daria Redes primero, y ordenar
        // "cuatri" como texto tambien ("10C" < "9C"). Las dos estarian mal.
        QCOMPARE(m, QStringList({QStringLiteral("Sistemas de Control"),
                                 QStringLiteral("Redes Industriales")}));
    }

    // ── Prompt ──

    void prompt_numeraFragmentosYCitaPaginas()
    {
        QVector<StudiaFragmento> frags;
        StudiaFragmento a;
        a.documento = QStringLiteral("ogata.pdf");
        a.materia   = QStringLiteral("Sistemas de Control");
        a.pagina    = 266;
        a.texto     = QStringLiteral("Criterio de Routh.");
        frags << a;
        StudiaFragmento b;
        b.documento = QStringLiteral("modbus.pdf");
        b.materia   = QStringLiteral("Redes Industriales");
        b.pagina    = 0;    // sin pagina conocida
        b.texto     = QStringLiteral("Modbus TCP.");
        frags << b;

        const QString p = StudiaPrompt::usuario(
            QStringLiteral("¿Qué es el criterio de Routh?"), frags);
        QVERIFY(p.contains(QStringLiteral("[1] Sistemas de Control · ogata.pdf · pág. 266")));
        QVERIFY(p.contains(QStringLiteral("[2] Redes Industriales · modbus.pdf")));
        QVERIFY(!p.contains(QStringLiteral("modbus.pdf · pág.")));   // sin pagina no la inventa
        QVERIFY(p.contains(QStringLiteral("Criterio de Routh.")));
        QVERIFY(p.contains(QStringLiteral("¿Qué es el criterio de Routh?")));
    }

    void prompt_recortaFragmentosLargos()
    {
        QVector<StudiaFragmento> frags;
        StudiaFragmento a;
        a.documento = QStringLiteral("largo.pdf");
        a.materia   = QStringLiteral("X");
        a.texto     = QString(5000, QLatin1Char('a'));
        frags << a;
        const QString p = StudiaPrompt::usuario(QStringLiteral("hola"), frags);
        // El cuerpo se acota; el prompt no puede arrastrar los 5000 caracteres.
        QVERIFY(p.size() < 3000);
    }

    void prompt_incluyeElHistorialComoContextoNoComoFuente()
    {
        QVector<StudiaFragmento> frags;
        StudiaFragmento a;
        a.documento = QStringLiteral("x.pdf");
        a.materia = QStringLiteral("Máquinas Eléctricas");
        a.texto = QStringLiteral("El motor de inducción tiene rotor jaula de ardilla.");
        frags << a;

        const QVector<StudiaPrompt::Turno> hist{
            {QStringLiteral("usuario"),   QStringLiteral("¿qué es un motor a inducción?")},
            {QStringLiteral("asistente"), QStringLiteral("Es una máquina asincrónica...")}};

        const QString p = StudiaPrompt::usuario(QStringLiteral("¿y cómo funciona?"),
                                                frags, hist);
        QVERIFY(p.contains(QStringLiteral("Conversación previa")));
        QVERIFY(p.contains(QStringLiteral("NO es documentación")));
        QVERIFY(p.contains(QStringLiteral("¿qué es un motor a inducción?")));
        // El historial va ANTES que los fragmentos y la pregunta al final.
        QVERIFY(p.indexOf(QStringLiteral("Conversación previa"))
                < p.indexOf(QStringLiteral("Fragmentos de la documentación")));
        QVERIFY(p.indexOf(QStringLiteral("Fragmentos de la documentación"))
                < p.indexOf(QStringLiteral("Pregunta del estudiante")));
    }

    void prompt_sinHistorialNoInventaLaSeccion()
    {
        QVector<StudiaFragmento> frags;
        StudiaFragmento a;
        a.documento = QStringLiteral("x.pdf");
        a.texto = QStringLiteral("texto");
        frags << a;
        const QString p = StudiaPrompt::usuario(QStringLiteral("hola"), frags);
        QVERIFY(!p.contains(QStringLiteral("Conversación previa")));
    }

    // ── Contexto conversacional (repreguntas) ──

    // ── Encuadre: sostenerse sola / continuar / cambiar de tema ──

    void encuadre_conTerminosEspecificosEsAutonoma()
    {
        // "criterio de Routh Hurwitz": varios términos propios del corpus.
        QCOMPARE(StudiaPrompt::encuadrar(4, 4, 3), StudiaPrompt::Encuadre::Autonoma);
        QCOMPARE(StudiaPrompt::encuadrar(2, 2, 2), StudiaPrompt::Encuadre::Autonoma);
    }

    void encuadre_sinTerminosPropiosEsDependiente()
    {
        // "¿y cómo funciona?" no deja nada buscable: continúa lo anterior.
        QCOMPARE(StudiaPrompt::encuadrar(0, 0, 0), StudiaPrompt::Encuadre::Dependiente);
    }

    void encuadre_terminosGenericosQueSiExistenEsDependiente()
    {
        // "¿qué pasa si aumento la frecuencia?": los términos existen en el
        // corpus pero no ubican el tema. Se completa con lo anterior.
        QCOMPARE(StudiaPrompt::encuadrar(2, 2, 1), StudiaPrompt::Encuadre::Dependiente);
    }

    void encuadre_terminosQueNoExistenEnElCorpusEsAjena()
    {
        // "¿cómo hago milanesas?": aporta términos y NINGUNO está en el corpus.
        // Es un cambio de tema, no una repregunta: no debe heredar nada.
        QCOMPARE(StudiaPrompt::encuadrar(1, 0, 0), StudiaPrompt::Encuadre::Ajena);
        QCOMPARE(StudiaPrompt::encuadrar(3, 0, 0), StudiaPrompt::Encuadre::Ajena);
    }

    void contexto_preguntaAutonomaNoSeExpande()
    {
        const QString original = QStringLiteral("qué es el criterio de Routh Hurwitz");
        QCOMPARE(StudiaPrompt::consultaConContexto(
                     original, {QStringLiteral("hablame de termodinámica")},
                     StudiaPrompt::Encuadre::Autonoma),
                 original);
    }

    void contexto_preguntaAjenaNoHeredaElTemaAnterior()
    {
        // El bug reportado: "/plan/ ¿cómo hago milanesas?" heredaba la charla
        // previa sobre Redes y terminaba armando un plan de estudio de Bluetooth.
        const QString q = StudiaPrompt::consultaConContexto(
            QStringLiteral("¿cómo hago milanesas?"),
            {QStringLiteral("explicame el protocolo Modbus"),
             QStringLiteral("qué es una VLAN")},
            StudiaPrompt::Encuadre::Ajena);
        QVERIFY2(!q.contains(QStringLiteral("Modbus"), Qt::CaseInsensitive), qPrintable(q));
        QVERIFY2(!q.contains(QStringLiteral("VLAN"), Qt::CaseInsensitive), qPrintable(q));
        QVERIFY(q.contains(QStringLiteral("milanesas")));
    }

    void contexto_repreguntaCortaHeredaElTema()
    {
        // "¿y cómo funciona?" no deja términos: sin contexto el sistema se
        // abstenía aunque el material estuviera. Ahora hereda el tema anterior.
        const QString q = StudiaPrompt::consultaConContexto(
            QStringLiteral("¿y cómo funciona?"),
            {QStringLiteral("qué es un motor a inducción")},
            StudiaPrompt::Encuadre::Dependiente);
        QVERIFY(q.contains(QStringLiteral("motor")));
        QVERIFY(q.contains(QStringLiteral("induccion"))
                || q.contains(QStringLiteral("inducción")));
    }

    void contexto_preguntaVagaConPalabrasGenericasSeExpande()
    {
        const QString q = StudiaPrompt::consultaConContexto(
            QStringLiteral("¿qué pasa si aumento la frecuencia?"),
            {QStringLiteral("explicame el motor de inducción")},
            StudiaPrompt::Encuadre::Dependiente);
        QVERIFY2(q.contains(QStringLiteral("motor")), qPrintable(q));
        QVERIFY2(q.contains(QStringLiteral("frecuencia")), qPrintable(q));
    }

    void contexto_repreguntaConservaSusPropiosTerminos()
    {
        const QString q = StudiaPrompt::consultaConContexto(
            QStringLiteral("¿y el rotor?"),
            {QStringLiteral("qué es un motor a inducción")},
            StudiaPrompt::Encuadre::Dependiente);
        QVERIFY(q.contains(QStringLiteral("rotor")));   // lo propio va primero
        QVERIFY(q.startsWith(QStringLiteral("rotor")));
        QVERIFY(q.contains(QStringLiteral("motor")));
    }

    void contexto_sinAnterioresDevuelveLoQueHay()
    {
        // Primera pregunta de la sesión, corta y sin historial.
        const QString q = StudiaPrompt::consultaConContexto(
            QStringLiteral("¿y eso?"), {}, StudiaPrompt::Encuadre::Dependiente);
        QVERIFY(q.trimmed().isEmpty());   // no hay términos: el gate se abstiene
    }

    void contexto_noRepiteTerminosNiDesborda()
    {
        QStringList previas;
        for (int i = 0; i < 10; ++i)
            previas << QStringLiteral("termino%1 motor motor motor").arg(i);
        const QString q = StudiaPrompt::consultaConContexto(
            QStringLiteral("¿y eso?"), previas, StudiaPrompt::Encuadre::Dependiente);
        const QStringList t = q.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        QVERIFY(t.size() <= StudiaPrompt::kMaxTerminosExpandida);
        QCOMPARE(t.count(QStringLiteral("motor")), 1);   // sin repetidos
    }

    // ── Modos de tutor ──

    void modos_catalogoTieneLosEsperados()
    {
        QStringList ids;
        for (const StudiaPrompt::Modo &m : StudiaPrompt::modos())
            ids << m.id;
        QVERIFY(ids.contains(QStringLiteral("libre")));
        QVERIFY(ids.contains(QStringLiteral("resumen")));
        QVERIFY(ids.contains(QStringLiteral("explicacion")));
        QVERIFY(ids.contains(QStringLiteral("autoevaluacion")));
        QVERIFY(ids.contains(QStringLiteral("flashcards")));
        QVERIFY(ids.contains(QStringLiteral("ejercitacion")));
        QVERIFY(ids.contains(QStringLiteral("plan")));
        // Todos menos "libre" aportan una consigna al prompt.
        for (const StudiaPrompt::Modo &m : StudiaPrompt::modos()) {
            if (m.id == StudiaPrompt::idModoLibre()) continue;
            QVERIFY2(!m.instruccion.isEmpty(), qPrintable(m.id));
            QVERIFY2(!m.etiqueta.isEmpty(), qPrintable(m.id));
        }
    }

    void modos_separaElPrefijo()
    {
        QString id, texto;
        StudiaPrompt::separarModo(QStringLiteral("/flashcards/ herramientas CNC"), &id, &texto);
        QCOMPARE(id, QStringLiteral("flashcards"));
        QCOMPARE(texto, QStringLiteral("herramientas CNC"));
    }

    void modos_prefijoToleranteAEspaciosYMayusculas()
    {
        QString id, texto;
        StudiaPrompt::separarModo(QStringLiteral("  / Flashcards /  algo "), &id, &texto);
        QCOMPARE(id, QStringLiteral("flashcards"));
        QCOMPARE(texto, QStringLiteral("algo"));

        StudiaPrompt::separarModo(QStringLiteral("/resumen/sin espacio"), &id, &texto);
        QCOMPARE(id, QStringLiteral("resumen"));
        QCOMPARE(texto, QStringLiteral("sin espacio"));
    }

    void modos_prefijoDesconocidoQuedaComoTexto()
    {
        QString id, texto;
        StudiaPrompt::separarModo(QStringLiteral("/inventado/ hola"), &id, &texto);
        QCOMPARE(id, StudiaPrompt::idModoLibre());
        QCOMPARE(texto, QStringLiteral("/inventado/ hola"));
    }

    void modos_sinPrefijoEsModoLibre()
    {
        QString id, texto;
        StudiaPrompt::separarModo(QStringLiteral("¿qué es un motor?"), &id, &texto);
        QCOMPARE(id, StudiaPrompt::idModoLibre());
        QCOMPARE(texto, QStringLiteral("¿qué es un motor?"));
        // Una división en el texto no debe confundirse con un prefijo.
        StudiaPrompt::separarModo(QStringLiteral("calculá 10/2 por favor"), &id, &texto);
        QCOMPARE(id, StudiaPrompt::idModoLibre());
    }

    void modos_elSistemaIncorporaLaConsigna()
    {
        const QString s = StudiaPrompt::sistema(QStringLiteral("Sistemas de Control"),
                                                QStringLiteral("flashcards"));
        QVERIFY(s.contains(QStringLiteral("MODO FLASHCARDS")));
        QVERIFY(s.contains(QStringLiteral("Sistemas de Control")));
        // El modo libre no agrega la consigna de ningún modo con formato fijo.
        const QString libre = StudiaPrompt::sistema(QStringLiteral("X"),
                                                    StudiaPrompt::idModoLibre());
        for (const StudiaPrompt::Modo &m : StudiaPrompt::modos()) {
            if (m.instruccion.isEmpty()) continue;
            const QString cabecera = m.instruccion.section(QLatin1Char('.'), 0, 0);
            QVERIFY2(!libre.contains(cabecera), qPrintable(m.id));
        }
    }

    void prompt_sistemaPideMarkdownYProhibeLatex()
    {
        const QString s = StudiaPrompt::sistema(QStringLiteral("X"),
                                                StudiaPrompt::idModoLibre());
        QVERIFY(s.contains(QStringLiteral("Markdown")));
        QVERIFY(s.contains(QStringLiteral("LaTeX")));
    }

    void prompt_sistemaFijaLaMateria()
    {
        const QString s = StudiaPrompt::sistema(QStringLiteral("Economía 1"),
                                                StudiaPrompt::idModoLibre());
        QVERIFY(s.contains(QStringLiteral("Economía 1")));
        QVERIFY(s.contains(QStringLiteral("no mezcles contenido de")));
        // Sin materia no inventa la sección.
        QVERIFY(!StudiaPrompt::sistema(QString(), StudiaPrompt::idModoLibre())
                     .contains(QStringLiteral("Esta conversación es sobre la materia")));
    }

    // ── Fuentes agrupadas por documento ──

    void fuentes_agrupanPaginasDelMismoDocumento()
    {
        QVector<StudiaFragmento> frags;
        auto mk = [](const QString &doc, const QString &ruta, int pag) {
            StudiaFragmento f;
            f.documento = doc; f.ruta = ruta; f.pagina = pag;
            f.materia = QStringLiteral("Sistemas de Control");
            return f;
        };
        frags << mk(QStringLiteral("ogata.pdf"), QStringLiteral("C:/a/ogata.pdf"), 266)
              << mk(QStringLiteral("clase.pdf"), QStringLiteral("C:/a/clase.pdf"), 1)
              << mk(QStringLiteral("ogata.pdf"), QStringLiteral("C:/a/ogata.pdf"), 308)
              << mk(QStringLiteral("ogata.pdf"), QStringLiteral("C:/a/ogata.pdf"), 12);

        const QVariantList f = StudiaController::agruparFuentes(frags);
        QCOMPARE(f.size(), 2);   // dos documentos, no cuatro entradas
        const QVariantMap a = f.at(0).toMap();
        QCOMPARE(a.value(QStringLiteral("documento")).toString(), QStringLiteral("ogata.pdf"));
        QCOMPARE(a.value(QStringLiteral("refs")).toString(), QStringLiteral("1, 3, 4"));
        // Paginas ordenadas de menor a mayor, no en el orden de relevancia.
        QCOMPARE(a.value(QStringLiteral("paginas")).toString(), QStringLiteral("12, 266, 308"));
        QCOMPARE(f.at(1).toMap().value(QStringLiteral("documento")).toString(),
                 QStringLiteral("clase.pdf"));
    }

    void fuentes_noRepitenPaginaDuplicada()
    {
        QVector<StudiaFragmento> frags;
        for (int i = 0; i < 2; ++i) {
            StudiaFragmento f;
            f.documento = QStringLiteral("x.pdf");
            f.ruta = QStringLiteral("C:/x.pdf");
            f.pagina = 5;              // dos fragmentos de la MISMA pagina
            frags << f;
        }
        const QVariantMap m = StudiaController::agruparFuentes(frags).at(0).toMap();
        QCOMPARE(m.value(QStringLiteral("paginas")).toString(), QStringLiteral("5"));
        QCOMPARE(m.value(QStringLiteral("refs")).toString(), QStringLiteral("1, 2"));
    }

    void fuentes_sinPaginaDejanElCampoVacio()
    {
        QVector<StudiaFragmento> frags;
        StudiaFragmento f;
        f.documento = QStringLiteral("notas.txt");
        f.ruta = QStringLiteral("C:/notas.txt");
        f.pagina = 0;                  // txt/docx no tienen paginas
        frags << f;
        const QVariantMap m = StudiaController::agruparFuentes(frags).at(0).toMap();
        QVERIFY(m.value(QStringLiteral("paginas")).toString().isEmpty());
    }

    void fuentes_distinguenArchivosHomonimos()
    {
        // Mismo nombre en dos materias: son fuentes distintas.
        QVector<StudiaFragmento> frags;
        StudiaFragmento a;
        a.documento = QStringLiteral("apunte.pdf");
        a.ruta = QStringLiteral("C:/redes/apunte.pdf");
        a.pagina = 1;
        StudiaFragmento b = a;
        b.ruta = QStringLiteral("C:/control/apunte.pdf");
        b.pagina = 2;
        frags << a << b;
        QCOMPARE(StudiaController::agruparFuentes(frags).size(), 2);
    }

    void fuentes_vaciasDevuelvenListaVacia()
    {
        QVERIFY(StudiaController::agruparFuentes({}).isEmpty());
    }

    void prompt_sistemaIncluyeLaAbstencion()
    {
        const QString s = StudiaPrompt::sistema(QStringLiteral("X"),
                                                StudiaPrompt::idModoLibre());
        QVERIFY(s.contains(StudiaPrompt::fraseAbstencion()));
        QVERIFY(s.contains(QStringLiteral("Mecatrónica")));
        QVERIFY(s.contains(QStringLiteral("[n]")));
    }

    // ── Conversión de LaTeX a texto legible ──

    void latex_fraccionSimple()
    {
        QCOMPARE(StudiaTexto::latexALegible(QStringLiteral("\\frac{a}{b}")),
                 QStringLiteral("a/b"));
        // Con expresiones compuestas hacen falta paréntesis.
        QCOMPARE(StudiaTexto::latexALegible(QStringLiteral("\\frac{a+b}{c}")),
                 QStringLiteral("(a+b)/c"));
    }

    void latex_fraccionAnidada()
    {
        QCOMPARE(StudiaTexto::latexALegible(QStringLiteral("\\frac{\\frac{a}{b}}{c}")),
                 QStringLiteral("(a/b)/c"));
    }

    void latex_superindicesYSubindices()
    {
        QCOMPARE(StudiaTexto::latexALegible(QStringLiteral("x^2")), QStringLiteral("x²"));
        QCOMPARE(StudiaTexto::latexALegible(QStringLiteral("a_n")), QStringLiteral("aₙ"));
        QCOMPARE(StudiaTexto::latexALegible(QStringLiteral("a_{n-1}")), QStringLiteral("aₙ₋₁"));
        QCOMPARE(StudiaTexto::latexALegible(QStringLiteral("d^{n}")), QStringLiteral("dⁿ"));
    }

    void latex_indiceSinEquivalenteQuedaLegible()
    {
        // 'z' no tiene subíndice Unicode: se conserva la notación con _.
        const QString r = StudiaTexto::latexALegible(QStringLiteral("a_{zzz}"));
        QVERIFY(r.contains(QStringLiteral("zzz")));
        QVERIFY(!r.contains(QLatin1Char('{')));
    }

    void latex_simbolosYGriegas()
    {
        QCOMPARE(StudiaTexto::latexALegible(QStringLiteral("\\alpha + \\beta")),
                 QStringLiteral("α + β"));
        QCOMPARE(StudiaTexto::latexALegible(QStringLiteral("a \\cdot b \\leq c")),
                 QStringLiteral("a · b ≤ c"));
        QCOMPARE(StudiaTexto::latexALegible(QStringLiteral("\\ldots")),
                 QStringLiteral("…"));
        // \leq no debe partirse por culpa de \le.
        QVERIFY(!StudiaTexto::latexALegible(QStringLiteral("\\leq")).contains(QLatin1Char('q')));
    }

    void latex_quitaDelimitadores()
    {
        QCOMPARE(StudiaTexto::latexALegible(QStringLiteral("$x^2$")).trimmed(),
                 QStringLiteral("x²"));
        QCOMPARE(StudiaTexto::latexALegible(QStringLiteral("\\(x^2\\)")).trimmed(),
                 QStringLiteral("x²"));
        QCOMPARE(StudiaTexto::latexALegible(QStringLiteral("\\[x^2\\]")).trimmed(),
                 QStringLiteral("x²"));
    }

    void latex_raizYTexto()
    {
        QCOMPARE(StudiaTexto::latexALegible(QStringLiteral("\\sqrt{2}")),
                 QStringLiteral("√(2)"));
        QCOMPARE(StudiaTexto::latexALegible(QStringLiteral("\\text{si } x>0")),
                 QStringLiteral("si x>0"));
    }

    void latex_ecuacionDiferencialCompleta()
    {
        // El caso real que reportó el usuario.
        const QString entrada = QStringLiteral(
            "a_n \\frac{d^n y(t)}{dt^n} + a_{n-1} \\frac{d^{n-1} y(t)}{dt^{n-1}} "
            "+ \\ldots + a_0 y(t) = b_m \\frac{d^m u(t)}{dt^m}");
        const QString r = StudiaTexto::latexALegible(entrada);
        // Nada de sintaxis LaTeX sobreviviente.
        QVERIFY2(!r.contains(QStringLiteral("\\frac")), qPrintable(r));
        QVERIFY2(!r.contains(QLatin1Char('{')), qPrintable(r));
        QVERIFY2(!r.contains(QLatin1Char('}')), qPrintable(r));
        QVERIFY2(!r.contains(QStringLiteral("\\ldots")), qPrintable(r));
        // Y los índices quedaron en Unicode.
        QVERIFY2(r.contains(QStringLiteral("aₙ")), qPrintable(r));
        QVERIFY2(r.contains(QStringLiteral("dⁿ")), qPrintable(r));
        QVERIFY2(r.contains(QStringLiteral("…")), qPrintable(r));
        QVERIFY2(r.contains(QLatin1Char('/')), qPrintable(r));
        // Las letras también van a superíndice: d^m debe quedar dᵐ, no "d^m".
        QVERIFY2(!r.contains(QLatin1Char('^')), qPrintable(r));
        QVERIFY2(r.contains(QStringLiteral("dᵐ")), qPrintable(r));
        QVERIFY2(r.contains(QStringLiteral("bₘ")), qPrintable(r));
    }

    void latex_letrasComoSuperindice()
    {
        QCOMPARE(StudiaTexto::latexALegible(QStringLiteral("e^t")), QStringLiteral("eᵗ"));
        QCOMPARE(StudiaTexto::latexALegible(QStringLiteral("x^k")), QStringLiteral("xᵏ"));
        QCOMPARE(StudiaTexto::latexALegible(QStringLiteral("d^m")), QStringLiteral("dᵐ"));
        // 'q' no tiene superíndice Unicode: se conserva legible, sin llaves.
        const QString conQ = StudiaTexto::latexALegible(QStringLiteral("a^{q}"));
        QVERIFY(conQ.contains(QLatin1Char('q')));
        QVERIFY(!conQ.contains(QLatin1Char('{')));
    }

    void latex_textoNormalNoSeToca()
    {
        const QString normal = QStringLiteral(
            "## Motores\nEl **par motor** depende de la corriente. Ver la tabla 3.");
        QCOMPARE(StudiaTexto::latexALegible(normal), normal);
    }

    void latex_noRompeMarkdown()
    {
        // Los guiones bajos de un nombre de archivo no son subíndices Markdown,
        // pero sí los toma la conversión: lo importante es que no se pierda texto.
        const QString r = StudiaTexto::latexALegible(QStringLiteral("archivo_uno.pdf"));
        QVERIFY(r.contains(QStringLiteral("archivo")));
        QVERIFY(r.contains(QStringLiteral(".pdf")));
    }

    void latex_esIdempotente()
    {
        const QString entrada = QStringLiteral("\\frac{a_1}{b^2} + \\alpha");
        const QString unaVez = StudiaTexto::latexALegible(entrada);
        QCOMPARE(StudiaTexto::latexALegible(unaVez), unaVez);
    }

    void latex_entradaVaciaOSinMatematica()
    {
        QCOMPARE(StudiaTexto::latexALegible(QString()), QString());
        QCOMPARE(StudiaTexto::latexALegible(QStringLiteral("hola")), QStringLiteral("hola"));
    }

    void latex_malformadoNoCuelga()
    {
        // Llave sin cerrar: no debe entrar en bucle ni perder el contenido.
        const QString r = StudiaTexto::latexALegible(QStringLiteral("\\frac{a}{b"));
        QVERIFY(r.contains(QLatin1Char('a')));
        QVERIFY(r.contains(QLatin1Char('b')));
    }

    // ── La abstención la hace cumplir el sistema, no el modelo ──

    void abstencion_cortaElFormatoQueVieneDespues()
    {
        // El caso reportado: dice la frase y a continuación arma el plan igual.
        const QString frase = StudiaPrompt::fraseAbstencion();
        const QString respuesta = frase + QStringLiteral(
            "\n\n## Alcance\nLa documentación no aborda las milanesas.\n"
            "## Sesiones\n| 1 | Bluetooth | [1] |\n");
        QCOMPARE(StudiaTexto::recortarTrasAbstencion(respuesta, frase), frase);
    }

    void abstencion_toleraUnaAperturaBreve()
    {
        const QString frase = StudiaPrompt::fraseAbstencion();
        const QString respuesta = QStringLiteral("Lamentablemente, ") + frase
                                  + QStringLiteral("\n\n## Alcance\nblah");
        const QString r = StudiaTexto::recortarTrasAbstencion(respuesta, frase);
        QVERIFY(r.endsWith(frase));
        QVERIFY(!r.contains(QStringLiteral("Alcance")));
    }

    void abstencion_noTocaUnaRespuestaLegitima()
    {
        // Responder y ACLARAR al final que otra parte no está es correcto.
        const QString frase = StudiaPrompt::fraseAbstencion();
        const QString respuesta = QString(400, QLatin1Char('x'))
                                  + QStringLiteral("\nSobre el segundo punto: ") + frase;
        QCOMPARE(StudiaTexto::recortarTrasAbstencion(respuesta, frase), respuesta);
    }

    void abstencion_sinLaFraseDejaTodoIgual()
    {
        const QString r = QStringLiteral("## Resumen\nTodo bien.");
        QCOMPARE(StudiaTexto::recortarTrasAbstencion(r, StudiaPrompt::fraseAbstencion()), r);
        QCOMPARE(StudiaTexto::recortarTrasAbstencion(r, QString()), r);
    }

    // ── HTML con interlineado (texto seleccionable) ──

    void html_llevaElInterlineadoQueTextEditNoTiene()
    {
        // El punto del ejercicio: `Text` tiene lineHeight pero no deja
        // seleccionar; `TextEdit` deja seleccionar pero no tiene lineHeight.
        // Si el interlineado no viaja en el HTML, se pierde el formato que ya
        // estaba andando, y no hay forma de notarlo desde el código.
        const QString h = StudiaTexto::aHtmlConInterlineado(
            QStringLiteral("Un párrafo."), false, 150);
        QVERIFY2(h.contains(QStringLiteral("line-height")),
                 qPrintable(QStringLiteral("sin line-height en el HTML: ") + h));
    }

    void html_conservaElTamanoDelCuerpo()
    {
        // Al exportar, Qt escribe el font-size del documento en el <body>, y
        // ese le gana al font.pixelSize del TextEdit. Sin fijarlo, el texto
        // salía con el 9pt por defecto de Qt (~12px) y se veía más chico que
        // cuando era un Text con pixelSize 16.
        const QString h = StudiaTexto::aHtmlConInterlineado(
            QStringLiteral("Un párrafo."), false, 150, 16);
        QVERIFY2(h.contains(QStringLiteral("font-size:12pt")),
                 qPrintable(QStringLiteral("cuerpo con tamaño equivocado: ") + h));
    }

    void html_losTitulosEscalanConElCuerpo()
    {
        // Qt exporta los encabezados como `x-large`, que es relativo: si el
        // cuerpo cambia de tamaño, los títulos acompañan solos.
        const QString h = StudiaTexto::aHtmlConInterlineado(
            QStringLiteral("## Título\n\ntexto"), true);
        QVERIFY(h.contains(QStringLiteral("x-large")));
    }

    void html_elMarkdownSigueSiendoMarkdown()
    {
        const QString h = StudiaTexto::aHtmlConInterlineado(
            QStringLiteral("## Título\n\nTexto con **negrita** y una lista:\n\n- uno\n- dos"),
            true);
        QVERIFY(h.contains(QStringLiteral("<h2")) || h.contains(QStringLiteral("<h1")));
        QVERIFY(h.contains(QStringLiteral("font-weight")));
        QVERIFY(h.contains(QStringLiteral("<li")));
        QVERIFY(!h.contains(QStringLiteral("**")));      // se interpretó, no se ve
    }

    void html_elMensajeDelUsuarioNoSeInterpreta()
    {
        // Lo que escribe el estudiante va tal cual: si pregunta por "a * b * c"
        // no se le puede comer los asteriscos como si fuera énfasis.
        const QString h = StudiaTexto::aHtmlConInterlineado(
            QStringLiteral("cuánto vale *a* por **b**"), false);
        QVERIFY(h.contains(QStringLiteral("*a*")));
        QVERIFY(h.contains(QStringLiteral("**b**")));
    }

    void html_escapaLoQueRompeElMarcado()
    {
        const QString h = StudiaTexto::aHtmlConInterlineado(
            QStringLiteral("if (a < b && c > d) <script>"), false);
        QVERIFY(!h.contains(QStringLiteral("<script>")));
        QVERIFY(h.contains(QStringLiteral("&lt;")));
    }

    void prompt_laReglaDeAbstencionVaDespuesDelModo()
    {
        // Es lo que evita que la plantilla del modo le gane a la abstención:
        // en un prompt, la instrucción más cercana al final pesa más.
        const QString s = StudiaPrompt::sistema(QStringLiteral("X"),
                                                QStringLiteral("plan"));
        const int modo  = s.indexOf(QStringLiteral("MODO PLAN"));
        const int regla = s.indexOf(QStringLiteral("REGLA QUE MANDA"));
        QVERIFY(modo > 0);
        QVERIFY2(regla > modo, "la regla de abstención debe cerrar el prompt");
        QVERIFY(s.contains(QStringLiteral("NO completes el formato del modo")));
    }

    void prompt_losEjemplosDeFiguraSonPlantillas()
    {
        // El modelo copiaba los ejemplos tal cual porque parecían contenido.
        const QString s = StudiaPrompt::sistema(QStringLiteral("X"),
                                                StudiaPrompt::idModoLibre());
        QVERIFY(s.contains(QStringLiteral("PLANTILLAS")));
        QVERIFY(s.contains(QStringLiteral("<expresión en función de x>")));
        // Y ya no lleva una función concreta que se pueda copiar sin pensar.
        QVERIFY2(!s.contains(QStringLiteral("x**2 - 3*x + 2")), "quedó un ejemplo copiable");
    }

    // ── Bloques: ecuaciones separadas del texto ──

    void bloques_textoSinEcuacionesEsUnSoloBloque()
    {
        const QVariantList b = StudiaTexto::enBloques(
            QStringLiteral("## Motores\nEl par depende de la corriente."));
        QCOMPARE(b.size(), 1);
        QCOMPARE(b.first().toMap().value(QStringLiteral("tipo")).toString(),
                 QStringLiteral("texto"));
    }

    void bloques_separaEcuacionDeDisplay()
    {
        const QVariantList b = StudiaTexto::enBloques(QStringLiteral(
            "La ecuación es:\n\\[ a_n x^2 + b \\]\ny se resuelve así."));
        QCOMPARE(b.size(), 3);
        QCOMPARE(b.at(0).toMap().value(QStringLiteral("tipo")).toString(),
                 QStringLiteral("texto"));
        QCOMPARE(b.at(1).toMap().value(QStringLiteral("tipo")).toString(),
                 QStringLiteral("ecuacion"));
        QCOMPARE(b.at(2).toMap().value(QStringLiteral("tipo")).toString(),
                 QStringLiteral("texto"));
        // La ecuación ya viene convertida a texto legible.
        const QString ec = b.at(1).toMap().value(QStringLiteral("contenido")).toString();
        QVERIFY2(ec.contains(QStringLiteral("aₙ")), qPrintable(ec));
        QVERIFY2(ec.contains(QStringLiteral("x²")), qPrintable(ec));
        QVERIFY(!ec.contains(QLatin1Char('\\')));
    }

    void bloques_reconoceDolarDoble()
    {
        const QVariantList b = StudiaTexto::enBloques(
            QStringLiteral("Sea:\n$$ E = m c^2 $$\nfin."));
        QCOMPARE(b.size(), 3);
        QCOMPARE(b.at(1).toMap().value(QStringLiteral("tipo")).toString(),
                 QStringLiteral("ecuacion"));
        QVERIFY(b.at(1).toMap().value(QStringLiteral("contenido")).toString()
                    .contains(QStringLiteral("c²")));
    }

    void bloques_reconoceRenglonEntreCorchetes()
    {
        // Como llega cuando el Markdown se comió las barras de \[ \].
        const QVariantList b = StudiaTexto::enBloques(QStringLiteral(
            "La fórmula:\n[ a_n \\frac{d y}{dt} + b_0 ]\nDonde a es constante."));
        QCOMPARE(b.size(), 3);
        QCOMPARE(b.at(1).toMap().value(QStringLiteral("tipo")).toString(),
                 QStringLiteral("ecuacion"));
    }

    void bloques_corchetesSinLatexNoSonEcuacion()
    {
        // Una cita [1] o una lista entre corchetes no debe volverse ecuación.
        const QVariantList b = StudiaTexto::enBloques(
            QStringLiteral("Según el apunte:\n[ ver la tabla 3 ]\nEso es todo."));
        for (const QVariant &v : b)
            QCOMPARE(v.toMap().value(QStringLiteral("tipo")).toString(),
                     QStringLiteral("texto"));
    }

    void bloques_variasEcuacionesSeguidas()
    {
        const QVariantList b = StudiaTexto::enBloques(QStringLiteral(
            "Uno:\n\\[ x^2 \\]\nDos:\n\\[ y^3 \\]\nfin."));
        int ecuaciones = 0;
        for (const QVariant &v : b)
            if (v.toMap().value(QStringLiteral("tipo")).toString()
                == QLatin1String("ecuacion"))
                ++ecuaciones;
        QCOMPARE(ecuaciones, 2);
    }

    void bloques_entradaVaciaNoDaBloques()
    {
        QVERIFY(StudiaTexto::enBloques(QString()).isEmpty());
        QVERIFY(StudiaTexto::enBloques(QStringLiteral("   \n  ")).isEmpty());
    }

    void bloques_ecuacionSolaSinTextoAlrededor()
    {
        const QVariantList b = StudiaTexto::enBloques(QStringLiteral("\\[ x^2 \\]"));
        QCOMPARE(b.size(), 1);
        QCOMPARE(b.first().toMap().value(QStringLiteral("tipo")).toString(),
                 QStringLiteral("ecuacion"));
    }

    void bloques_lineaSueltaConFormulaEsEcuacion()
    {
        // El modelo escribe la fórmula en su renglón, sin delimitadores.
        const QVariantList b = StudiaTexto::enBloques(QStringLiteral(
            "Se calcula así:\n2x/2 = 43/2\nY se despeja x"));
        bool hayEcuacion = false;
        for (const QVariant &v : b)
            if (v.toMap().value(QStringLiteral("tipo")).toString()
                == QLatin1String("ecuacion"))
                hayEcuacion = true;
        QVERIFY(hayEcuacion);
    }

    void bloques_formulaEntreBackticksEsEcuacion()
    {
        // El caso de la captura: el modelo la envuelve como si fuera código.
        QVERIFY(StudiaTexto::esLineaEcuacion(
            QStringLiteral("`∫∫S f(x, y, z) dS = ∫∫D f(r(u, v)) · ‖ru × rv‖ dA`")));
    }

    void esLineaEcuacion_rechazaProsa()
    {
        // Prosa con una fórmula adentro: no es un bloque de ecuación.
        QVERIFY(!StudiaTexto::esLineaEcuacion(
            QStringLiteral("La velocidad v = d/t es constante durante el tramo")));
        QVERIFY(!StudiaTexto::esLineaEcuacion(
            QStringLiteral("Donde D es el dominio de la parametrización.")));
        QVERIFY(!StudiaTexto::esLineaEcuacion(QStringLiteral("En resumen, las integrales")));
    }

    void esLineaEcuacion_rechazaEstructuraMarkdown()
    {
        QVERIFY(!StudiaTexto::esLineaEcuacion(QStringLiteral("## Título = importante")));
        QVERIFY(!StudiaTexto::esLineaEcuacion(QStringLiteral("- item con x = 2")));
        QVERIFY(!StudiaTexto::esLineaEcuacion(QStringLiteral("1. paso con x = 2")));
        QVERIFY(!StudiaTexto::esLineaEcuacion(QStringLiteral("| a | b = c |")));
    }

    void esLineaEcuacion_aceptaFormulasTipicas()
    {
        QVERIFY(StudiaTexto::esLineaEcuacion(QStringLiteral("2x/2 = 43/2")));
        QVERIFY(StudiaTexto::esLineaEcuacion(QStringLiteral("V = I · R")));
        QVERIFY(StudiaTexto::esLineaEcuacion(QStringLiteral("n = 120·f/p")));
        QVERIFY(StudiaTexto::esLineaEcuacion(QStringLiteral("aₙ x² + bₙ x + c = 0")));
    }

    void esLineaEcuacion_rechazaTextoSinMatematica()
    {
        QVERIFY(!StudiaTexto::esLineaEcuacion(QStringLiteral("hola")));
        QVERIFY(!StudiaTexto::esLineaEcuacion(QString()));
        // Y algo larguísimo tampoco: sería un párrafo, no una fórmula.
        QVERIFY(!StudiaTexto::esLineaEcuacion(QString(300, QLatin1Char('=')) ));
    }

    // ── Búsqueda semántica ──

    void embed_normalizaANorma1()
    {
        const QVector<float> v = StudiaEmbed::normalizar({3.0f, 4.0f});
        QVERIFY(qAbs(v[0] - 0.6f) < 1e-5f);
        QVERIFY(qAbs(v[1] - 0.8f) < 1e-5f);
        // El vector nulo no se puede normalizar: se devuelve tal cual.
        QCOMPARE(StudiaEmbed::normalizar({0.0f, 0.0f}), QVector<float>({0.0f, 0.0f}));
    }

    void embed_parseaLaRespuestaDelServidor()
    {
        QString err;
        const QVector<float> v = StudiaEmbed::parsearRespuesta(
            R"({"data":[{"index":0,"embedding":[0.1,0.2,0.3]}]})", &err);
        QCOMPARE(v.size(), 3);
        QVERIFY(err.isEmpty());
    }

    void embed_avisaSiElServidorNoTieneEmbeddings()
    {
        QString err;
        // llama-server sin --embeddings responde 200 con un error adentro.
        QVERIFY(StudiaEmbed::parsearRespuesta(
            R"({"error":{"message":"embeddings disabled"}})", &err).isEmpty());
        QVERIFY2(err.contains(QStringLiteral("embeddings")), qPrintable(err));

        QVERIFY(StudiaEmbed::parsearRespuesta(QByteArrayLiteral("{}"), &err).isEmpty());
        QVERIFY(!err.isEmpty());
        QVERIFY(StudiaEmbed::parsearRespuesta(QByteArrayLiteral("no es json"), &err).isEmpty());
        QVERIFY(!err.isEmpty());
    }

    void indice_sinVectoresElHibridoCaeALexico()
    {
        StudiaIndex idx;
        QVERIFY(idx.abrir(m_db));
        idx.setUmbralAbstencion(0.0);
        QVERIFY(!idx.tieneVectores());        // el índice de prueba no está vectorizado
        // Con vector vacío o sin vectores en la base, el resultado es el léxico.
        const auto lexico  = idx.buscar(QStringLiteral("criterio de estabilidad de Routh"), 5);
        const auto hibrido = idx.buscarHibrido(
            QStringLiteral("criterio de estabilidad de Routh"), {}, 5);
        QCOMPARE(hibrido.size(), lexico.size());
        QVERIFY(!hibrido.isEmpty());
        QCOMPARE(hibrido.first().documento, lexico.first().documento);
    }

    void indice_losFragmentosTraenSuId()
    {
        // El id es la clave con la que se fusionan los dos rankings.
        StudiaIndex idx;
        QVERIFY(idx.abrir(m_db));
        idx.setUmbralAbstencion(0.0);
        const auto r = idx.buscar(QStringLiteral("criterio de estabilidad de Routh"), 3);
        QVERIFY(!r.isEmpty());
        for (const auto &f : r)
            QVERIFY2(f.fragId > 0, qPrintable(f.documento));
    }

    void controlador_estadoSemanticoSeReporta()
    {
        StudiaController c;
        QVERIFY(c.abrirIndice(m_db));
        c.setUrlEmbeddings(QString());
        QVERIFY(!c.semanticaActiva());        // sin servidor no hay semántica
        const QVariantMap e = c.estadoSemantico();
        QCOMPARE(e.value(QStringLiteral("vectores")).toInt(), 0);
        QVERIFY(e.value(QStringLiteral("fragmentos")).toInt() > 0);

        // Con servidor pero sin índice vectorizado, tampoco.
        c.setUrlEmbeddings(QStringLiteral("http://127.0.0.1:8081"));
        QVERIFY(!c.semanticaActiva());
        QCOMPARE(c.urlEmbeddings(), QStringLiteral("http://127.0.0.1:8081"));
        c.setUrlEmbeddings(QString());
    }

    // ── Sinónimos coloquiales: las dos formas de preguntar deben coincidir ──

    void terminos_pasaYSucedeNoAlteranLaConsulta()
    {
        // Eran sinónimos que BM25 pesaba distinto según su frecuencia, y por eso
        // "¿qué pasa si…?" y "¿qué sucede si…?" se comportaban distinto.
        const QStringList a = StudiaIndex::terminosConsulta(
            QStringLiteral("¿qué pasa si aumento la frecuencia?"));
        const QStringList b = StudiaIndex::terminosConsulta(
            QStringLiteral("¿qué sucede si aumento la frecuencia?"));
        const QStringList c = StudiaIndex::terminosConsulta(
            QStringLiteral("¿qué ocurre si aumento la frecuencia?"));
        QCOMPARE(a, b);
        QCOMPARE(a, c);
        QCOMPARE(a, QStringList({QStringLiteral("aumento"), QStringLiteral("frecuencia")}));
    }

    void terminos_conservaLosVerbosTecnicos()
    {
        // "genera", "produce", "afecta" SÍ discriminan en un corpus técnico:
        // no deben caer en la lista de palabras vacías.
        const QStringList t = StudiaIndex::terminosConsulta(
            QStringLiteral("qué genera el estator y cómo afecta al rotor"));
        QVERIFY(t.contains(QStringLiteral("genera")));
        QVERIFY(t.contains(QStringLiteral("afecta")));
        QVERIFY(t.contains(QStringLiteral("estator")));
    }

    // ── Bloques de imagen: diagramas y gráficos ──

    void bloques_reconoceGraficoYMermaid()
    {
        const QVariantList b = StudiaTexto::enBloques(QStringLiteral(
            "Mirá la curva:\n"
            "```grafico\nfuncion: x**2\nrango: -3, 3\n```\n"
            "Y el proceso:\n"
            "```mermaid\nflowchart TD\n  A --> B\n```\n"
            "Eso es todo."));
        QStringList tipos;
        for (const QVariant &v : b)
            tipos << v.toMap().value(QStringLiteral("tipo")).toString();
        QVERIFY2(tipos.contains(QStringLiteral("grafico")), qPrintable(tipos.join(',')));
        QVERIFY2(tipos.contains(QStringLiteral("mermaid")), qPrintable(tipos.join(',')));
        QVERIFY(tipos.contains(QStringLiteral("texto")));
    }

    void bloques_elContenidoDelGraficoNoSeToca()
    {
        // El spec lo consume matplotlib: no debe pasar por la conversión de
        // LaTeX (x**2 no es un superíndice).
        const QVariantList b = StudiaTexto::enBloques(QStringLiteral(
            "```grafico\nfuncion: x**2 - 3*x\nrango: -2, 5\n```"));
        QCOMPARE(b.size(), 1);
        const QString c = b.first().toMap().value(QStringLiteral("contenido")).toString();
        QVERIFY2(c.contains(QStringLiteral("x**2")), qPrintable(c));
        QVERIFY(c.contains(QStringLiteral("rango: -2, 5")));
    }

    void bloques_otrosLenguajesDeCodigoNoSonImagen()
    {
        const QVariantList b = StudiaTexto::enBloques(QStringLiteral(
            "Ejemplo:\n```python\nprint(1)\n```\nfin"));
        for (const QVariant &v : b) {
            const QString t = v.toMap().value(QStringLiteral("tipo")).toString();
            QVERIFY2(t == QLatin1String("texto"), qPrintable(t));
        }
    }

    // ── Flashcards → Anki ──

    void flashcards_rotuloYContenidoEnLaMismaLinea()
    {
        // Como lo escribe el modelo en la práctica: "**1. Frente** Pregunta".
        // Con el parser anterior no reconocía ninguna tarjeta y el botón de
        // exportar nunca aparecía.
        const QString respuesta = QStringLiteral(
            "**1. Frente** Fórmulas del producto\n\n"
            "**Dorso** Las fórmulas del producto son: sen x cos y = ... [2]\n\n"
            "---\n");
        const QVariantList t = StudiaTexto::flashcards(respuesta);
        QCOMPARE(t.size(), 1);
        QCOMPARE(t.first().toMap().value(QStringLiteral("frente")).toString(),
                 QStringLiteral("Fórmulas del producto"));
        QVERIFY(t.first().toMap().value(QStringLiteral("dorso")).toString()
                    .startsWith(QStringLiteral("Las fórmulas")));
    }

    void flashcards_noConfundePalabrasQueEmpiezanIgual()
    {
        // "Frentes de onda" no es el rótulo "Frente".
        QVERIFY(StudiaTexto::flashcards(
            QStringLiteral("Frentes de onda planos\ny su propagación")).isEmpty());
    }

    void flashcards_extraeLasTarjetas()
    {
        const QString respuesta = QStringLiteral(
            "**1. Frente**\n¿Qué es el deslizamiento?\n\n"
            "**Dorso**\nLa diferencia relativa entre velocidad de sincronismo y "
            "la real. [1]\n\n---\n\n"
            "**2. Frente**\n¿Qué es el par motor?\n\n"
            "**Dorso**\nLa cuota de torque que entrega el eje. [2]\n\n---\n");
        const QVariantList t = StudiaTexto::flashcards(respuesta);
        QCOMPARE(t.size(), 2);
        QCOMPARE(t.first().toMap().value(QStringLiteral("frente")).toString(),
                 QStringLiteral("¿Qué es el deslizamiento?"));
        QVERIFY(t.first().toMap().value(QStringLiteral("dorso")).toString()
                    .contains(QStringLiteral("sincronismo")));
    }

    void flashcards_toleraOtrosFormatosDeRotulo()
    {
        const QString respuesta = QStringLiteral(
            "### Frente:\npregunta uno\n### Dorso:\nrespuesta uno\n---\n");
        QCOMPARE(StudiaTexto::flashcards(respuesta).size(), 1);
    }

    void flashcards_respuestaSinTarjetasDaVacio()
    {
        QVERIFY(StudiaTexto::flashcards(
            QStringLiteral("## Resumen\nEsto es un resumen normal.")).isEmpty());
        QVERIFY(StudiaTexto::flashcards(QString()).isEmpty());
    }

    void flashcards_tsvUsaTabYUnaLineaPorTarjeta()
    {
        QVariantList t;
        t.append(QVariantMap{{QStringLiteral("frente"), QStringLiteral("¿Qué es X?")},
                             {QStringLiteral("dorso"),  QStringLiteral("Es algo.")}});
        t.append(QVariantMap{{QStringLiteral("frente"), QStringLiteral("¿Y Z?")},
                             {QStringLiteral("dorso"),  QStringLiteral("Línea 1\nLínea 2")}});
        const QString tsv = StudiaTexto::flashcardsATsv(t);
        const QStringList filas = tsv.split(QLatin1Char('\n'));
        QCOMPARE(filas.size(), 2);
        QVERIFY(filas.first().contains(QLatin1Char('\t')));
        QCOMPARE(filas.first().count(QLatin1Char('\t')), 1);
        // Los saltos internos van como <br>: si no, romperían el renglón.
        QVERIFY(filas.at(1).contains(QStringLiteral("<br>")));
        QCOMPARE(filas.at(1).count(QLatin1Char('\t')), 1);
    }

    void flashcards_descartaTarjetasIncompletas()
    {
        QVariantList t;
        t.append(QVariantMap{{QStringLiteral("frente"), QStringLiteral("sola")},
                             {QStringLiteral("dorso"),  QString()}});
        QVERIFY(StudiaTexto::flashcardsATsv(t).isEmpty());
    }

    // ── Prompt de imágenes ──

    void prompt_explicaComoPedirGraficosYDiagramas()
    {
        const QString s = StudiaPrompt::sistema(QStringLiteral("X"),
                                                StudiaPrompt::idModoLibre());
        // Los dos formatos y las claves obligatorias del spec.
        QVERIFY(s.contains(QStringLiteral("```grafico")));
        QVERIFY(s.contains(QStringLiteral("```mermaid")));
        QVERIFY(s.contains(QStringLiteral("funcion:")));
        QVERIFY(s.contains(QStringLiteral("rango:")));
        // Y es IMPERATIVO ante un pedido de graficar: el modelo estaba
        // explicando cómo hacerlo en vez de emitir el bloque.
        QVERIFY2(s.contains(QStringLiteral("DEBE")), "falta la instrucción imperativa");
        QVERIFY(s.contains(QStringLiteral("graficá")));
    }

    // ── Rigor por modo y respuesta desde la conversación ──

    void modos_conversacionEsFlexibleYFlashcardsExigente()
    {
        QVERIFY(!StudiaPrompt::modoPorId(QStringLiteral("libre")).exigente);
        QVERIFY(!StudiaPrompt::modoPorId(QStringLiteral("explicacion")).exigente);
        QVERIFY(!StudiaPrompt::modoPorId(QStringLiteral("ejercitacion")).exigente);
        QVERIFY(StudiaPrompt::modoPorId(QStringLiteral("flashcards")).exigente);
        QVERIFY(StudiaPrompt::modoPorId(QStringLiteral("autoevaluacion")).exigente);
        QVERIFY(StudiaPrompt::modoPorId(QStringLiteral("resumen")).exigente);
        QVERIFY(StudiaPrompt::modoPorId(QStringLiteral("plan")).exigente);
    }

    void prompt_modoFlexiblePideAgotarAntesDeAbstenerse()
    {
        const QString libre = StudiaPrompt::sistema(QStringLiteral("X"),
                                                    StudiaPrompt::idModoLibre());
        QVERIFY(libre.contains(QStringLiteral("ACTITUD EN ESTE MODO")));
        QVERIFY(libre.contains(QStringLiteral("Reservá la frase de abstención")));
        // El modo exigente NO lleva esa licencia.
        const QString fc = StudiaPrompt::sistema(QStringLiteral("X"),
                                                 QStringLiteral("flashcards"));
        QVERIFY(!fc.contains(QStringLiteral("ACTITUD EN ESTE MODO")));
    }

    void conversacion_detectaSiHayDeQueAgarrarse()
    {
        QVERIFY(!StudiaPrompt::puedeResponderDesdeConversacion({}));
        // Sólo preguntas del usuario no alcanzan: hace falta una respuesta previa.
        QVERIFY(!StudiaPrompt::puedeResponderDesdeConversacion(
            {{QStringLiteral("usuario"), QStringLiteral("hola, qué tal todo por acá")}}));
        // Una respuesta corta tampoco.
        QVERIFY(!StudiaPrompt::puedeResponderDesdeConversacion(
            {{QStringLiteral("asistente"), QStringLiteral("ok")}}));
        QVERIFY(StudiaPrompt::puedeResponderDesdeConversacion(
            {{QStringLiteral("asistente"),
              QStringLiteral("El motor de inducción tiene un rotor jaula de ardilla "
                             "y un estator con bobinado trifásico.")}}));
    }

    void prompt_soloConversacionNoPideCitas()
    {
        const QVector<StudiaPrompt::Turno> h{
            {QStringLiteral("usuario"),   QStringLiteral("¿qué es un motor?")},
            {QStringLiteral("asistente"), QStringLiteral("Es una máquina que convierte "
                                                         "energía eléctrica en mecánica.")}};
        const QString p = StudiaPrompt::usuarioSoloConversacion(
            QStringLiteral("repetí la ecuación anterior"), h);
        QVERIFY(p.contains(QStringLiteral("Conversación hasta ahora")));
        QVERIFY(p.contains(QStringLiteral("repetí la ecuación anterior")));
        QVERIFY(p.contains(QStringLiteral("no trajo material nuevo")));
        // No debe pedir citas [n]: en ese turno no hay fragmentos numerados.
        QVERIFY(p.contains(QStringLiteral("No cites")));
    }

    // ── Modo Ejercitación ──

    void modos_incluyeEjercitacion()
    {
        const StudiaPrompt::Modo m = StudiaPrompt::modoPorId(QStringLiteral("ejercitacion"));
        QCOMPARE(m.id, QStringLiteral("ejercitacion"));
        QCOMPARE(m.etiqueta, QStringLiteral("Ejercitación"));
        QVERIFY(!m.instruccion.isEmpty());
        const QString s = StudiaPrompt::sistema(QStringLiteral("Física 1"),
                                                QStringLiteral("ejercitacion"));
        QVERIFY(s.contains(QStringLiteral("MODO EJERCITACIÓN")));
        QVERIFY(s.contains(QStringLiteral("Desarrollo")));
    }

    void modos_elNombreViejoSigueLlegandoAlMismoModo()
    {
        // Hay chats guardados con modo "ejercicio": sin el alias quedarían sin
        // etiqueta ni color al reabrirlos.
        QCOMPARE(StudiaPrompt::modoPorId(QStringLiteral("ejercicio")).id,
                 QStringLiteral("ejercitacion"));
        QString id, texto;
        StudiaPrompt::separarModo(QStringLiteral("/ejercicio/ una viga"), &id, &texto);
        QCOMPARE(id, QStringLiteral("ejercitacion"));
        QCOMPARE(texto, QStringLiteral("una viga"));
        // Y el que se escribe con tilde también.
        StudiaPrompt::separarModo(QStringLiteral("/Ejercitación/ otra"), &id, &texto);
        QCOMPARE(id, QStringLiteral("ejercitacion"));
    }

    // ── Personalidad de cada modo ──

    void modos_cadaUnoTieneSuColorMenosConversacion()
    {
        const QHash<QString, QString> esperado = {
            {QStringLiteral("resumen"),        QStringLiteral("#3B82F6")},
            {QStringLiteral("explicacion"),    QStringLiteral("#EAB308")},
            {QStringLiteral("autoevaluacion"), QStringLiteral("#8B5CF6")},
            {QStringLiteral("flashcards"),     QStringLiteral("#F97316")},
            {QStringLiteral("ejercitacion"),   QStringLiteral("#EF4444")},
            {QStringLiteral("plan"),           QStringLiteral("#22C55E")},
        };
        for (auto it = esperado.cbegin(); it != esperado.cend(); ++it)
            QCOMPARE(StudiaPrompt::modoPorId(it.key()).color, it.value());
        // Conversación es el modo normal: usa el color estándar de la app.
        QVERIFY(StudiaPrompt::modoPorId(StudiaPrompt::idModoLibre()).color.isEmpty());
    }

    void modos_elColorLlegaAQml()
    {
        bool halloResumen = false;
        for (const QVariant &v : StudiaPrompt::modosParaQml()) {
            const QVariantMap m = v.toMap();
            QVERIFY(m.contains(QStringLiteral("color")));
            QVERIFY(m.contains(QStringLiteral("ocultaRespuestas")));
            if (m.value(QStringLiteral("id")).toString() == QLatin1String("resumen")) {
                halloResumen = true;
                QCOMPARE(m.value(QStringLiteral("color")).toString(),
                         QStringLiteral("#3B82F6"));
            }
        }
        QVERIFY(halloResumen);
    }

    void modos_soloAutoevaluacionYFlashcardsOcultanRespuestas()
    {
        QVERIFY(StudiaPrompt::modoPorId(QStringLiteral("autoevaluacion")).ocultaRespuestas);
        QVERIFY(StudiaPrompt::modoPorId(QStringLiteral("flashcards")).ocultaRespuestas);
        for (const QString &id : {QStringLiteral("libre"), QStringLiteral("resumen"),
                                  QStringLiteral("explicacion"),
                                  QStringLiteral("ejercitacion"), QStringLiteral("plan")})
            QVERIFY2(!StudiaPrompt::modoPorId(id).ocultaRespuestas, qPrintable(id));
    }

    void modos_losQueOcultanRespuestasPidenParesPreguntaRespuesta()
    {
        // Lo que sostiene el plegado: que el modelo escriba cada pregunta con
        // su respuesta. De ahí el sistema arma las dos mitades.
        for (const QString &id : {QStringLiteral("autoevaluacion"),
                                  QStringLiteral("flashcards")}) {
            const StudiaPrompt::Modo m = StudiaPrompt::modoPorId(id);
            QVERIFY2(m.ocultaRespuestas, qPrintable(id));
            const QString f = StudiaPrompt::recordatorioDeFormato(id);
            QVERIFY2(f.contains(QStringLiteral("P: ")), qPrintable(id));
            QVERIFY2(f.contains(QStringLiteral("R: ")), qPrintable(id));
        }
        // Y los demás modos no llevan recordatorio: no tienen formato estricto.
        for (const QString &id : {QStringLiteral("libre"), QStringLiteral("resumen"),
                                  QStringLiteral("explicacion"),
                                  QStringLiteral("ejercitacion")})
            QVERIFY2(StudiaPrompt::recordatorioDeFormato(id).isEmpty(), qPrintable(id));
    }

    void modos_elSeparadorEsInconfundible()
    {
        const QString s = StudiaPrompt::separadorRespuestas();
        QVERIFY(s.contains(QStringLiteral("RESPUESTAS")));
        // Más largo que el "---" con el que se separan las flashcards.
        QVERIFY(s.count(QLatin1Char('-')) >= 5);
    }

    void modos_elResumenNoSeParezcaAUnaConversacion()
    {
        // Se veía casi igual a la respuesta del modo Conversación. Lo que lo
        // distingue es la forma: prosa condensada, no una ficha con títulos.
        const QString s = StudiaPrompt::sistema(QStringLiteral("X"),
                                                QStringLiteral("resumen"));
        QVERIFY(s.contains(QStringLiteral("PROSA CORRIDA")));
        QVERIFY(s.contains(QStringLiteral("Nada de títulos")));
        // Y deja claro que no está contestando la pregunta, sino condensando.
        QVERIFY(s.contains(QStringLiteral("no estás")));
    }

    void modos_autoevaluacionNoPreguntaLoQueNoSabeResponder()
    {
        const QString s = StudiaPrompt::sistema(QStringLiteral("X"),
                                                QStringLiteral("autoevaluacion"));
        QVERIFY(s.contains(QStringLiteral("No preguntes nada que no puedas responder")));
    }

    void modos_flashcardsSonMasCortasQueLaAutoevaluacion()
    {
        // Es la diferencia entre repasar y estudiar: flashcards pone un tope
        // de palabras, autoevaluación pide explicar y justificar.
        QVERIFY(StudiaPrompt::recordatorioDeFormato(QStringLiteral("flashcards"))
                    .contains(QStringLiteral("20 palabras")));
        const QString a = StudiaPrompt::sistema(QStringLiteral("X"),
                                                QStringLiteral("autoevaluacion"));
        QVERIFY(a.contains(QStringLiteral("explicar, aplicar o justificar")));
    }

    void zz_volcarPromptsParaProbarContraElModelo()
    {
        // Escribe los prompts de sistema tal como los recibe el modelo, para
        // poder mandarlos al servidor real y ver qué devuelve de verdad. Los
        // tests de texto verifican que la instrucción esté; sólo el modelo
        // dice si la obedece.
        const QString dir = QDir::tempPath() + QStringLiteral("/studia_prompts");
        QDir().mkpath(dir);
        struct Caso { QString archivo; QString modo; bool planConDatos; };
        const QVector<Caso> casos = {
            {QStringLiteral("autoevaluacion"), QStringLiteral("autoevaluacion"), false},
            {QStringLiteral("flashcards"),     QStringLiteral("flashcards"),     false},
            {QStringLiteral("plan1"),          QStringLiteral("plan"),           false},
            {QStringLiteral("plan2"),          QStringLiteral("plan"),           true},
        };
        for (const Caso &c : casos) {
            QFile f(dir + QStringLiteral("/") + c.archivo + QStringLiteral(".txt"));
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                f.write(StudiaPrompt::sistema(QStringLiteral("Redes de Comunicación "
                                                             "Industriales"),
                                              c.modo, c.planConDatos).toUtf8());
            }
            // El recordatorio de formato va aparte: lo agrega el mensaje de
            // usuario, después de los fragmentos.
            QFile g(dir + QStringLiteral("/") + c.archivo
                    + QStringLiteral("_formato.txt"));
            if (g.open(QIODevice::WriteOnly | QIODevice::Truncate))
                g.write(StudiaPrompt::recordatorioDeFormato(c.modo,
                                                            c.planConDatos).toUtf8());
        }
    }

    void modos_cadaConsignaTieneSuPropiaPersonalidad()
    {
        // Dos modos no pueden dar la misma instrucción: si coincidieran, la
        // diferencia entre ellos sería sólo el nombre del botón.
        QSet<QString> vistas;
        for (const StudiaPrompt::Modo &m : StudiaPrompt::modos()) {
            if (m.id == StudiaPrompt::idModoLibre()) continue;
            QVERIFY2(!vistas.contains(m.instruccion), qPrintable(m.id));
            vistas.insert(m.instruccion);
            // Y cada una se presenta diciendo qué modo es: es la primera línea
            // que lee el modelo y lo que lo saca de la conversación genérica.
            QVERIFY2(m.instruccion.startsWith(QStringLiteral("MODO ")),
                     qPrintable(m.id));
        }
    }

    // Conversación con una respuesta previa de contenido suficiente: hace falta
    // que supere el umbral de puedeResponderDesdeConversacion().
    static QVector<StudiaPrompt::Turno> historialConRespuesta()
    {
        return {
            {QStringLiteral("usuario"), QStringLiteral("/autoevaluacion/ integrales")},
            {QStringLiteral("asistente"),
             QStringLiteral("1. ¿Qué es una integral definida y qué representa "
                            "geométricamente?\n2. Calculá la integral de x dx "
                            "entre 0 y 2.")},
        };
    }

    // ── Abstenerse o apoyarse en lo ya dicho ──
    //
    // El fallo real: generaba 5 preguntas de autoevaluación y al pedirle las
    // respuestas contestaba que no tenía información sobre el tema. No puede
    // abstenerse de contestar preguntas que escribió él mismo.

    void conversacion_unaRepreguntaEnModoExigenteSeApoyaEnLoAnterior()
    {
        // "¿y por qué?" no aporta términos propios: es dependiente. Buscar en
        // el índice algo que la pregunta ni nombra, y abstenerse porque no
        // aparece, sería absurdo.
        QVERIFY(StudiaPrompt::respondeDesdeLaConversacion(
                    true, StudiaPrompt::Encuadre::Dependiente,
                    historialConRespuesta()));
    }

    void conversacion_unaPreguntaAjenaEnModoExigenteSeAbstiene()
    {
        // Acá sí corresponde abstenerse: cambia a un tema que el corpus no
        // cubre, y contestarlo con lo que se venía hablando sería inventar.
        QVERIFY(!StudiaPrompt::respondeDesdeLaConversacion(
                    true, StudiaPrompt::Encuadre::Ajena,
                    historialConRespuesta()));
    }

    void conversacion_unaRespuestaPreviaMuyCortaNoAlcanza()
    {
        // Una respuesta de dos palabras no es material del que apoyarse: hace
        // falta que la conversación previa tenga contenido real.
        const QVector<StudiaPrompt::Turno> h = {
            {QStringLiteral("usuario"),   QStringLiteral("integrales")},
            {QStringLiteral("asistente"), QStringLiteral("Sí, claro.")},
        };
        QVERIFY(!StudiaPrompt::puedeResponderDesdeConversacion(h));
        QVERIFY(!StudiaPrompt::respondeDesdeLaConversacion(
                    false, StudiaPrompt::Encuadre::Dependiente, h));
    }

    void conversacion_sinRespuestaPreviaNoHayDeDondeSacarNada()
    {
        const QVector<StudiaPrompt::Turno> vacio;
        const QVector<StudiaPrompt::Turno> soloPreguntas = {
            {QStringLiteral("usuario"), QStringLiteral("integrales")},
        };
        QVERIFY(!StudiaPrompt::respondeDesdeLaConversacion(
                    true, StudiaPrompt::Encuadre::Dependiente, vacio));
        QVERIFY(!StudiaPrompt::respondeDesdeLaConversacion(
                    false, StudiaPrompt::Encuadre::Dependiente, soloPreguntas));
    }

    void conversacion_elModoFlexibleSiempreIntenta()
    {
        QVERIFY(StudiaPrompt::respondeDesdeLaConversacion(
                    false, StudiaPrompt::Encuadre::Ajena,
                    historialConRespuesta()));
    }

    // ── La consigna se entrega sin resolver: lo garantiza el sistema ──

    void consigna_partePorLosParesPreguntaRespuesta()
    {
        // El formato que el modelo sostiene de verdad: cada pregunta con su
        // respuesta al lado. Partirlas en dos mitades es trabajo del sistema.
        const QString r = QStringLiteral(
            "1. P: ¿Qué establece el criterio de Routh?\n"
            "   R: Que la primera columna no cambie de signo. [1]\n"
            "2. P: ¿Cuántos cambios de signo admite un sistema estable?\n"
            "   R: Ninguno. [2]\n");
        const StudiaTexto::ConsignaPartida p = StudiaTexto::partirConsigna(r);
        QVERIFY(p.consigna.contains(QStringLiteral("criterio de Routh")));
        QVERIFY(!p.consigna.contains(QStringLiteral("primera columna")));
        QVERIFY(p.respuestas.contains(QStringLiteral("primera columna")));
        QVERIFY(!p.respuestas.contains(QStringLiteral("¿Qué establece")));
        // Numeradas de nuevo, para que se lean como una lista limpia.
        QVERIFY(p.consigna.startsWith(QStringLiteral("1. ")));
        QVERIFY(p.respuestas.startsWith(QStringLiteral("1. ")));
    }

    void consigna_toleraLasVariantesDeLosPares()
    {
        // Como las escribe el modelo cuando se desvía un poco del ejemplo.
        const QString r = QStringLiteral(
            "**1.** Pregunta: ¿Qué es la impedancia?\n"
            "**Respuesta:** La oposición al paso de corriente.\n"
            "2) P - ¿En qué se mide?\n"
            "2) R - En ohmios.\n");
        const QVector<StudiaTexto::ParQR> pares = StudiaTexto::paresQR(r);
        QCOMPARE(pares.size(), 2);
        QCOMPARE(pares.at(0).pregunta, QStringLiteral("¿Qué es la impedancia?"));
        QCOMPARE(pares.at(1).respuesta, QStringLiteral("En ohmios."));
    }

    void consigna_unParSinRespuestaSeDescarta()
    {
        // Si el modelo cortó a mitad de camino, esa pregunta no se muestra:
        // desplegar y no encontrar su respuesta es peor que no ofrecerla.
        const QString r = QStringLiteral(
            "1. P: Una pregunta\n   R: Su respuesta\n"
            "2. P: Otra pregunta que quedó sin contestar\n");
        const QVector<StudiaTexto::ParQR> pares = StudiaTexto::paresQR(r);
        QCOMPARE(pares.size(), 1);
    }

    void consigna_laRespuestaLargaSePegaEntera()
    {
        const QString r = QStringLiteral(
            "1. P: ¿Qué es el modelo OSI?\n"
            "   R: Un modelo de siete capas.\n"
            "   Cada capa tiene una función propia. [3]\n");
        const QVector<StudiaTexto::ParQR> pares = StudiaTexto::paresQR(r);
        QCOMPARE(pares.size(), 1);
        QVERIFY(pares.first().respuesta.contains(QStringLiteral("siete capas")));
        QVERIFY(pares.first().respuesta.contains(QStringLiteral("función propia")));
    }

    void consigna_partePorLaLineaSeparadora()
    {
        // Si el modelo escribe la línea en vez de los pares, también sirve.
        const QString r = QStringLiteral(
            "1. ¿Qué establece el criterio de Routh?\n"
            "2. ¿Cuántos cambios de signo admite un sistema estable?\n\n")
            + StudiaPrompt::separadorRespuestas() + QStringLiteral(
            "\n\n1. Que la primera columna no cambie de signo.\n2. Ninguno.");
        const StudiaTexto::ConsignaPartida p = StudiaTexto::partirConsigna(r);
        QVERIFY(p.consigna.contains(QStringLiteral("criterio de Routh")));
        QVERIFY(!p.consigna.contains(QStringLiteral("primera columna")));
        QVERIFY(p.respuestas.contains(QStringLiteral("primera columna")));
        QVERIFY(!p.respuestas.contains(QStringLiteral("¿Qué establece")));
    }

    void consigna_toleraVariantesDeLaLinea()
    {
        for (const QString &sep : {QStringLiteral("---RESPUESTAS---"),
                                   QStringLiteral("-------- respuestas --------"),
                                   QStringLiteral("--------")}) {
            const QString r = QStringLiteral("1. Una pregunta.\n\n") + sep
                              + QStringLiteral("\n\n1. Su respuesta.");
            const StudiaTexto::ConsignaPartida p = StudiaTexto::partirConsigna(r);
            QVERIFY2(p.respuestas.contains(QStringLiteral("Su respuesta")),
                     qPrintable(sep));
            QVERIFY2(!p.consigna.contains(QStringLiteral("Su respuesta")),
                     qPrintable(sep));
        }
    }

    void consigna_elGuionDeLasFlashcardsNoParteNada()
    {
        // Las tarjetas se separan entre sí con "---": si eso cortara el
        // mensaje, la primera tarjeta quedaría sola como consigna.
        const QString r = QStringLiteral(
            "**1. Frente**\nPregunta uno.\n\n**Dorso**\nRespuesta uno.\n\n"
            "---\n\n"
            "**2. Frente**\nPregunta dos.\n\n**Dorso**\nRespuesta dos.");
        const StudiaTexto::ConsignaPartida p = StudiaTexto::partirConsigna(r);
        QVERIFY(p.respuestas.isEmpty());
        QCOMPARE(p.consigna, r);
    }

    void consigna_sinSeparadorTodoEsConsigna()
    {
        // El modelo se olvidó de la línea: se muestra todo antes que ocultar
        // media respuesta o inventar dónde cortarla.
        const QString r = QStringLiteral("1. Una pregunta.\n2. Otra pregunta.");
        const StudiaTexto::ConsignaPartida p = StudiaTexto::partirConsigna(r);
        QCOMPARE(p.consigna, r);
        QVERIFY(p.respuestas.isEmpty());
    }

    void consigna_unSeparadorSueltoNoParteNada()
    {
        // Al principio no deja consigna; al final no deja respuestas. En los
        // dos casos partir no aporta nada y se devuelve el texto entero.
        for (const QString &r : {StudiaPrompt::separadorRespuestas()
                                     + QStringLiteral("\n\n1. Sí.\n2. No."),
                                 QStringLiteral("1. Una pregunta.\n\n")
                                     + StudiaPrompt::separadorRespuestas()}) {
            const StudiaTexto::ConsignaPartida p = StudiaTexto::partirConsigna(r);
            QVERIFY(p.respuestas.isEmpty());
            QCOMPARE(p.consigna, r);
        }
    }

    // ── Flashcards en el formato nuevo: numeradas, sin Frente/Dorso ──

    void flashcards_emparejaPorNumeroALosDosLadosDeLaLinea()
    {
        const QString r = QStringLiteral(
            "1. ¿Qué es la impedancia?\n"
            "2. ¿En qué unidad se mide?\n\n")
            + StudiaPrompt::separadorRespuestas() + QStringLiteral(
            "\n\n1. La oposición total al paso de corriente alterna.\n"
            "2. En ohmios (Ω).");
        const QVariantList t = StudiaTexto::flashcards(r);
        QCOMPARE(t.size(), 2);
        QCOMPARE(t.at(0).toMap().value(QStringLiteral("frente")).toString(),
                 QStringLiteral("¿Qué es la impedancia?"));
        QCOMPARE(t.at(0).toMap().value(QStringLiteral("dorso")).toString(),
                 QStringLiteral("La oposición total al paso de corriente alterna."));
        QCOMPARE(t.at(1).toMap().value(QStringLiteral("dorso")).toString(),
                 QStringLiteral("En ohmios (Ω)."));
    }

    void flashcards_siNoCoincidenLasCantidadesNoInventaTarjetas()
    {
        // Emparejar 3 preguntas con 2 respuestas dejaría tarjetas con la
        // respuesta de otra: es peor que no exportar ninguna.
        const QString r = QStringLiteral("1. Una.\n2. Dos.\n3. Tres.\n\n")
            + StudiaPrompt::separadorRespuestas()
            + QStringLiteral("\n\n1. Primera.\n2. Segunda.");
        QVERIFY(StudiaTexto::flashcards(r).isEmpty());
    }

    void flashcards_unNumeroDentroDeLaRespuestaNoAbreOtraTarjeta()
    {
        const QString r = QStringLiteral("1. ¿Cuándo se publicó?\n\n")
            + StudiaPrompt::separadorRespuestas()
            + QStringLiteral("\n\n1. En 1984, y se revisó luego.\n"
                             "   La versión vigente es de 2020.");
        const QVariantList t = StudiaTexto::flashcards(r);
        QCOMPARE(t.size(), 1);
        QVERIFY(t.first().toMap().value(QStringLiteral("dorso")).toString()
                    .contains(QStringLiteral("2020")));
    }

    void flashcards_elFormatoViejoConRotulosSigueAndando()
    {
        // Hay chats guardados con "Frente"/"Dorso": no pueden dejar de
        // exportarse porque cambió el formato nuevo.
        const QString r = QStringLiteral(
            "**1. Frente**\n¿Qué es la impedancia?\n\n"
            "**Dorso**\nLa oposición al paso de corriente. `[1]`\n");
        const QVariantList t = StudiaTexto::flashcards(r);
        QCOMPARE(t.size(), 1);
        QCOMPARE(t.first().toMap().value(QStringLiteral("frente")).toString(),
                 QStringLiteral("¿Qué es la impedancia?"));
    }

    void flashcards_seExportanConTabulacion()
    {
        const QString r = QStringLiteral("1. Pregunta.\n\n")
            + StudiaPrompt::separadorRespuestas()
            + QStringLiteral("\n\n1. Respuesta.");
        const QString tsv = StudiaTexto::flashcardsATsv(StudiaTexto::flashcards(r));
        QCOMPARE(tsv, QStringLiteral("Pregunta.\tRespuesta."));
    }

    void consigna_elFormatoLlegaAlFinalDelMensajeDeUsuario()
    {
        // Medido contra el modelo real: en el prompt de sistema lo ignoraba
        // —queda sepultado entre las reglas generales— y como última línea
        // antes de generar lo cumple.
        StudiaFragmento f;
        f.materia = QStringLiteral("Redes");
        f.documento = QStringLiteral("apunte.pdf");
        f.texto = QStringLiteral("El modelo OSI define siete capas.");
        const QString u = StudiaPrompt::usuario(QStringLiteral("el modelo OSI"),
                                                {f}, {},
                                                QStringLiteral("flashcards"));
        QVERIFY(u.contains(QStringLiteral("Cómo tiene que salir tu respuesta")));
        // Después de la pregunta, no antes.
        QVERIFY(u.indexOf(QStringLiteral("Cómo tiene que salir"))
                > u.indexOf(QStringLiteral("Pregunta del estudiante")));
        // Y en un modo sin formato estricto no se agrega nada.
        QVERIFY(!StudiaPrompt::usuario(QStringLiteral("x"), {f}, {},
                                       QStringLiteral("resumen"))
                     .contains(QStringLiteral("Cómo tiene que salir")));
    }

    void modos_elPlanLlevaUnaSolaConsignaPorTurno()
    {
        // La causa del desastre anterior: el prompt traía las dos consignas y
        // le pedía al modelo que dedujera en cuál estaba. Ahora lo decide el
        // sistema y va una sola.
        const QString primero = StudiaPrompt::sistema(QStringLiteral("X"),
                                                      QStringLiteral("plan"), false);
        QVERIFY(primero.contains(QStringLiteral("Temas que abarca")));
        QVERIFY(primero.contains(QStringLiteral("Para armártelo necesito saber")));
        // En el primer turno no aparece nada del plan.
        QVERIFY(!primero.contains(QStringLiteral("## Sesiones")));

        const QString segundo = StudiaPrompt::sistema(QStringLiteral("X"),
                                                      QStringLiteral("plan"), true);
        QVERIFY(segundo.contains(QStringLiteral("ya te pasó")));
        // Y en el segundo no queda rastro de volver a preguntar.
        QVERIFY(!segundo.contains(QStringLiteral("Para armártelo necesito saber")));
        // La estructura del plan va en el recordatorio de formato, que es el
        // que el modelo cumple.
        const QString f = StudiaPrompt::recordatorioDeFormato(
            QStringLiteral("plan"), true);
        QVERIFY(f.contains(QStringLiteral("## Sesiones")));
        QVERIFY(!StudiaPrompt::recordatorioDeFormato(QStringLiteral("plan"), false)
                     .contains(QStringLiteral("## Sesiones")));
    }

    void modos_elPlanHaceLasCuentasAntesDeLaTabla()
    {
        // Sin esto escribía la tabla de memoria: mismas horas para todos los
        // temas y días que el estudiante no tenía. Obligarlo a poner el total
        // y el reparto ANTES lo ata a los datos que le dieron.
        const QString f = StudiaPrompt::recordatorioDeFormato(
            QStringLiteral("plan"), true);
        QVERIFY(f.contains(QStringLiteral("Horas totales")));
        QVERIFY(f.contains(QStringLiteral("## Reparto")));
        QVERIFY(f.contains(QStringLiteral("suman exactamente")));
        // Las tres reglas que fallaban en la práctica: días de más, todos los
        // temas con las mismas horas, y la tabla ignorando su propio reparto.
        QVERIFY(f.contains(QStringLiteral("exactamente 3 filas")));
        QVERIFY(f.contains(QStringLiteral("DOBLE de horas")));
        QVERIFY(f.contains(QStringLiteral("UNA FILA POR DÍA")));
        QVERIFY(f.contains(QStringLiteral("EN «QUÉ ESTUDIAR» SE VE EL REPARTO")));
        // Y el reparto va antes que la tabla, no después.
        QVERIFY(f.indexOf(QStringLiteral("## Reparto"))
                < f.indexOf(QStringLiteral("## Sesiones")));
    }

    void modos_elTurnoDelPlanNoAfectaALosDemas()
    {
        // El parámetro es sólo del Plan: ningún otro modo cambia con él.
        for (const QString &id : {QStringLiteral("libre"), QStringLiteral("resumen"),
                                  QStringLiteral("flashcards"),
                                  QStringLiteral("autoevaluacion")}) {
            QCOMPARE(StudiaPrompt::sistema(QStringLiteral("X"), id, true),
                     StudiaPrompt::sistema(QStringLiteral("X"), id, false));
        }
    }

    void modos_elFormatoSeMuestraConUnEjemplo()
    {
        // Lo que un modelo de 7B sí sigue: ver la forma, no leer reglas.
        const QString a = StudiaPrompt::recordatorioDeFormato(
            QStringLiteral("autoevaluacion"));
        QVERIFY(a.contains(QStringLiteral("1. P: ")));
        QVERIFY(a.contains(QStringLiteral("…así hasta la 10")));

        const QString f = StudiaPrompt::recordatorioDeFormato(
            QStringLiteral("flashcards"));
        QVERIFY(f.contains(QStringLiteral("5. P: ")));
        QVERIFY(f.contains(QStringLiteral("Cinco pares")));
        QVERIFY(StudiaPrompt::sistema(QStringLiteral("X"),
                                      QStringLiteral("flashcards"))
                    .contains(QStringLiteral("No escribas «Frente» ni «Dorso»")));
    }

    void modos_lasConsignasEstrictasSonCortas()
    {
        // Un prompt largo es lo que rompió estos modos: el de sistema ya trae
        // reglas, formato e imágenes, y encima iba media página por modo. Si
        // vuelve a crecer, este test avisa antes que el uso real.
        for (const QString &id : {QStringLiteral("autoevaluacion"),
                                  QStringLiteral("flashcards"),
                                  QStringLiteral("plan")}) {
            const int n = StudiaPrompt::modoPorId(id).instruccion.size();
            QVERIFY2(n < 1200, qPrintable(QStringLiteral("%1: %2 caracteres")
                                              .arg(id).arg(n)));
        }
        QVERIFY(StudiaPrompt::instruccionPlanConDatos().size() < 1200);
    }

    void prompt_permiteRazonarPeroNoInventarHechos()
    {
        const QString s = StudiaPrompt::sistema(QStringLiteral("X"),
                                                StudiaPrompt::idModoLibre());
        // Debe habilitar explícitamente aplicar/relacionar…
        QVERIFY(s.contains(QStringLiteral("RAZONAR")));
        QVERIFY(s.contains(QStringLiteral("aplicar un método a un caso nuevo"))
                || s.contains(QStringLiteral("aplicar un método")));
        // …sin aflojar la regla sobre los hechos.
        QVERIFY(s.contains(QStringLiteral("No completes con conocimiento general")));
        QVERIFY(s.contains(StudiaPrompt::fraseAbstencion()));
    }

    // ── Bibliografía propia: el gate no aplica en un índice chico ──

    void indice_puedeDesactivarLaExigenciaDeEvidencia()
    {
        StudiaIndex idx;
        QVERIFY(idx.abrir(m_db));
        // Con el umbral imposible el gate rechaza todo...
        idx.setUmbralAbstencion(-1e9);
        QVERIFY(idx.buscar(QStringLiteral("criterio de estabilidad de Routh"), 5).isEmpty());
        // ...salvo que se desactive, que es lo que hace el índice de
        // bibliografía propia (ahí BM25 da ~0 y nada superaría el corte).
        idx.setExigirEvidencia(false);
        QVERIFY(!idx.buscar(QStringLiteral("criterio de estabilidad de Routh"), 5).isEmpty());
    }

    void indice_documentosListaLoIngestado()
    {
        StudiaIndex idx;
        QVERIFY(idx.abrir(m_db));
        const QVariantList todos = idx.documentos();
        QCOMPARE(todos.size(), 3);
        const QVariantList deControl = idx.documentos(QStringLiteral("Sistemas de Control"));
        QCOMPARE(deControl.size(), 1);
        const QVariantMap d = deControl.first().toMap();
        QCOMPARE(d.value(QStringLiteral("nombre")).toString(), QStringLiteral("ogata.pdf"));
        QCOMPARE(d.value(QStringLiteral("estado")).toString(), QStringLiteral("ok"));
        QVERIFY(d.contains(QStringLiteral("ruta")));
        QVERIFY(idx.documentos(QStringLiteral("No Existe")).isEmpty());
    }

    // ── Que la app se pueda entregar armada ──

    void empaquetado_elIndiceSeBuscaJuntoAlEjecutable()
    {
        // Sin esto, quien recibe la app tiene que salir a buscar el índice con
        // un diálogo de archivos: la ruta guardada es la de OTRA máquina.
        const QString esperado = QCoreApplication::applicationDirPath()
                                 + QStringLiteral("/StudIA/studia.db");
        QFile f(esperado);
        const bool habia = f.exists();
        if (!habia) {
            QDir().mkpath(QFileInfo(esperado).absolutePath());
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("x");
            f.close();
        }
        QCOMPARE(StudiaController::indiceEmpaquetado(), esperado);
        if (!habia) {
            QFile::remove(esperado);
            QCOMPARE(StudiaController::indiceEmpaquetado(), QString());
        }
    }

    void empaquetado_elIndiceGuardaDeDondeSalio()
    {
        // Es lo que permite recalcular dónde quedó cada documento cuando el
        // corpus viaja con la aplicación.
        StudiaIndex idx;
        QVERIFY(idx.abrir(m_db));
        QCOMPARE(idx.corpusRaiz(), QStringLiteral("C:/corpus"));
    }

    void empaquetado_unDocumentoSeReubicaEnLaCarpetaQueHaya()
    {
        // El caso real: el índice dice D:\...\DATA\... y en esta máquina el
        // corpus está en otro lado.
        StudiaController c;
        QVERIFY(c.abrirIndice(m_db));
        // Las rutas del índice de prueba (C:/corpus/...) no existen: sin una
        // carpeta donde reubicarlas, no se inventa ninguna.
        QVERIFY(c.reubicarDocumento(QStringLiteral("C:/corpus/ogata.pdf")).isEmpty());
        // Una ruta que ni siquiera cuelga de la raíz indexada, tampoco.
        QVERIFY(c.reubicarDocumento(QStringLiteral("Z:/no/existe.pdf")).isEmpty());
        QVERIFY(c.reubicarDocumento(QString()).isEmpty());
        // Un archivo que sí está se devuelve tal cual, sin tocar nada.
        QCOMPARE(c.reubicarDocumento(m_db), m_db);
    }

    void empaquetado_losDocumentosSeBuscanEnVariosLados()
    {
        // Los 7,7 GB de documentos no entran en el instalador, asi que llegan
        // aparte y pueden terminar en cualquier lado: copiados a C:\StudIA_Docs,
        // o leidos directo de un disco externo. La carpeta elegida por el
        // copiador tiene que pesar mas que la que quedo grabada al indexar.
        StudiaController c;
        QVERIFY(c.abrirIndice(m_db));

        QTemporaryDir copiados;
        QVERIFY(copiados.isValid());

        QSettings().remove(QStringLiteral("studia/carpetaDocumentos"));
        const QString sinNada = c.carpetaCorpus();

        QSettings().setValue(QStringLiteral("studia/carpetaDocumentos"),
                             copiados.path());
        QSettings().sync();
        QCOMPARE(c.carpetaCorpus(), copiados.path());
        QVERIFY(c.carpetaCorpus() != sinNada);

        // Si esa carpeta desaparece (el disco externo que se desconecto), no se
        // devuelve una ruta muerta: se sigue buscando en el resto.
        QSettings().setValue(QStringLiteral("studia/carpetaDocumentos"),
                             QStringLiteral("Z:/carpeta/que/no/existe"));
        QSettings().sync();
        QCOMPARE(c.carpetaCorpus(), sinNada);

        QSettings().remove(QStringLiteral("studia/carpetaDocumentos"));
        QSettings().sync();
    }

    void empaquetado_carpetaJuntoAlIndiceSaleDeLaRutaDelDb()
    {
        // Pura manipulacion de texto: la carpeta de documentos es la DATA_StudIA
        // hermana del .db. Sin esto habria que adivinar donde quedaron los PDF.
        QCOMPARE(StudiaController::carpetaJuntoAlIndice(
                     QStringLiteral("C:/donde/sea/studia.db")),
                 QStringLiteral("C:/donde/sea/DATA_StudIA"));

        // Sin indice abierto no hay carpeta que derivar.
        QVERIFY(StudiaController::carpetaJuntoAlIndice(QString()).isEmpty());
        QVERIFY(StudiaController::carpetaJuntoAlIndice(QStringLiteral("   ")).isEmpty());
    }

    void empaquetado_laCarpetaHermanaDelIndiceMandaSobreLasDemas()
    {
        // La instalacion recomendada es dejar studia.db y DATA_StudIA juntos y
        // elegir el .db desde la app: de ahi sale tambien donde estan los
        // documentos, sin copiarlos a una ruta fija ni registrar nada. Tiene que
        // ganarle a la carpeta que hubiera dejado el copiador.
        QTemporaryDir sede;
        QVERIFY(sede.isValid());
        const QString db = sede.filePath(QStringLiteral("studia.db"));
        QVERIFY(QFile::copy(m_db, db));
        const QString docs = sede.filePath(QStringLiteral("DATA_StudIA"));
        QVERIFY(QDir().mkpath(docs));

        QTemporaryDir otra;                 // la del copiador, que debe perder
        QVERIFY(otra.isValid());
        QSettings().setValue(QStringLiteral("studia/carpetaDocumentos"), otra.path());
        QSettings().sync();

        StudiaController c;
        QVERIFY(c.abrirIndice(db));
        QCOMPARE(QDir(c.carpetaCorpus()).canonicalPath(), QDir(docs).canonicalPath());

        // Si la hermana no existe, se sigue respetando la del copiador: no se
        // devuelve una ruta inventada.
        QVERIFY(QDir(docs).removeRecursively());
        QCOMPARE(QDir(c.carpetaCorpus()).canonicalPath(), QDir(otra.path()).canonicalPath());

        QSettings().remove(QStringLiteral("studia/carpetaDocumentos"));
        QSettings().sync();
    }

    void empaquetado_cuandoElDocumentoNoEstaSeExplicaPorQue()
    {
        // Un clic que no hace nada se lee como "se colgó". Y con la copia que se
        // entrega sin el corpus, ese clic pasa a ser el caso normal.
        StudiaController c;
        QVERIFY(c.abrirIndice(m_db));

        // Un archivo que está: no hay nada que explicar.
        QVERIFY(c.motivoDocumento(m_db).isEmpty());

        const QString sinRuta   = c.motivoDocumento(QString());
        const QString deCatedra = c.motivoDocumento(QStringLiteral("C:/corpus/ogata.pdf"));
        const QString suelta    = c.motivoDocumento(QStringLiteral("Z:/no/existe.pdf"));
        QVERIFY(!sinRuta.isEmpty());
        QVERIFY(!deCatedra.isEmpty());
        QVERIFY(!suelta.isEmpty());

        // Y sobre todo no dicen lo mismo: que esta copia no traiga el corpus es
        // esperado y el texto citado sigue estando; que falte un archivo suelto
        // es otra cosa. Se comparan entre sí para no atar el test a la
        // redacción, que ya cambió una vez.
        if (c.carpetaCorpus().isEmpty())
            QVERIFY(deCatedra != suelta);
        QVERIFY(sinRuta != deCatedra);
    }

    void servidorEmbeddings_diceQueLeFaltaSiNoEstaElModelo()
    {
        // Nunca puede quedar en silencio: si falta el modelo, la búsqueda sigue
        // siendo léxica y el estudiante tiene que enterarse por qué.
        StudiaEmbedServer s;
        QVERIFY(!s.activo());
        s.iniciar();
        // O levantó (hay modelo y binario en esta máquina), o explicó por qué no.
        QVERIFY(s.activo() || !s.motivo().isEmpty());
        s.detener();
    }

    void herramientas_informaCadaUnaYQueHabilita()
    {
        const QVector<StudiaHerramientas::Herramienta> hs = StudiaHerramientas::revisar();
        QVERIFY(hs.size() >= 4);
        QSet<QString> ids;
        for (const StudiaHerramientas::Herramienta &h : hs) {
            QVERIFY2(!h.id.isEmpty(), "sin id no se puede identificar");
            QVERIFY2(!h.nombre.isEmpty(), qPrintable(h.id));
            // Lo que importa para el estudiante: qué pierde si falta. Sin esto
            // el aviso diría "falta matplotlib" y no significaría nada.
            QVERIFY2(!h.habilita.isEmpty(), qPrintable(h.id));
            QVERIFY2(!ids.contains(h.id), qPrintable(h.id));
            ids.insert(h.id);
        }
        QVERIFY(ids.contains(QStringLiteral("python")));
        QVERIFY(ids.contains(QStringLiteral("tesseract")));
        QVERIFY(ids.contains(QStringLiteral("mermaid")));
    }

    void herramientas_elConteoCoincideConLaLista()
    {
        int n = 0;
        for (const StudiaHerramientas::Herramienta &h : StudiaHerramientas::revisar())
            if (!h.presente)
                ++n;
        QCOMPARE(StudiaHerramientas::faltantes(), n);
    }

    void herramientas_lleganAQmlConTodasLasClaves()
    {
        const QVariantList l = StudiaHerramientas::paraQml();
        QCOMPARE(l.size(), StudiaHerramientas::revisar().size());
        for (const QVariant &v : l) {
            const QVariantMap m = v.toMap();
            for (const QString &k : {QStringLiteral("id"), QStringLiteral("nombre"),
                                     QStringLiteral("habilita"), QStringLiteral("presente")})
                QVERIFY2(m.contains(k), qPrintable(k));
        }
    }

    void servidorEmbeddings_laUrlEsLaQueUsaElResto()
    {
        QCOMPARE(StudiaEmbedServer::url(),
                 QStringLiteral("http://127.0.0.1:%1").arg(StudiaEmbedServer::kPuerto));
    }

    // ── Temas: varias conversaciones por materia ──

    void titulo_saleDelPrimerTituloDeLaRespuesta()
    {
        // El modelo encabeza con el concepto; la pregunta suele ser coloquial.
        QCOMPARE(StudiaTexto::tituloDeRespuesta(QStringLiteral(
                     "## Criterio de Routh-Hurwitz\n\nEs un método que…")),
                 QStringLiteral("Criterio de Routh-Hurwitz"));
        QCOMPARE(StudiaTexto::tituloDeRespuesta(QStringLiteral(
                     "# Integrales definidas\ncontenido")),
                 QStringLiteral("Integrales definidas"));
        // El primero manda, aunque haya otros más abajo.
        QCOMPARE(StudiaTexto::tituloDeRespuesta(QStringLiteral(
                     "### Modbus TCP\n\ntexto\n\n## Otra cosa\n")),
                 QStringLiteral("Modbus TCP"));
    }

    void titulo_reconoceUnRenglonEnNegritaComoTitulo()
    {
        QCOMPARE(StudiaTexto::tituloDeRespuesta(QStringLiteral(
                     "**Ecuaciones de primer grado:**\n\nUna ecuación…")),
                 QStringLiteral("Ecuaciones de primer grado"));
        // Negrita en medio de una frase NO es un título.
        QCOMPARE(StudiaTexto::tituloDeRespuesta(QStringLiteral(
                     "Una **integral** es el área bajo la curva.")),
                 QStringLiteral("Una integral es el área bajo la curva"));
    }

    void titulo_sinTituloCaeALaPrimeraLinea()
    {
        QCOMPARE(StudiaTexto::tituloDeRespuesta(QStringLiteral(
                     "\n\nLa impedancia se mide en ohmios.\nY además…")),
                 QStringLiteral("La impedancia se mide en ohmios"));
        QVERIFY(StudiaTexto::tituloDeRespuesta(QStringLiteral("   \n\n")).isEmpty());
        QVERIFY(StudiaTexto::tituloDeRespuesta(QString()).isEmpty());
    }

    void titulo_largoSeCortaEnUnaPalabra()
    {
        const QString larga = QStringLiteral(
            "## Criterio de estabilidad de Routh Hurwitz aplicado a sistemas "
            "de tercer orden");
        const QString t = StudiaTexto::tituloDeRespuesta(larga);
        QVERIFY(t.size() <= StudiaTexto::kLargoTitulo + 1);   // +1 por el "…"
        QVERIFY(t.endsWith(QStringLiteral("…")));
        // No corta a mitad de palabra.
        const QString sinPunto = t.chopped(1);
        QVERIFY(larga.contains(sinPunto));
        QVERIFY(!sinPunto.endsWith(QLatin1Char(' ')));
    }

    void temas_variosPorMateriaYNoSeMezclan()
    {
        StudiaSessionStore st;
        const QString a = st.crear(QStringLiteral("Matemática 1"));
        const QString b = st.crear(QStringLiteral("Matemática 1"));
        const QString c = st.crear(QStringLiteral("Redes"));
        QVERIFY(!a.isEmpty() && a != b);
        QCOMPARE(st.cantidadDe(QStringLiteral("Matemática 1")), 2);
        QCOMPARE(st.cantidadDe(QStringLiteral("Redes")), 1);
        QCOMPARE(st.deMateria(QStringLiteral("Redes")).size(), 1);
        QCOMPARE(st.deMateria(QStringLiteral("Redes")).first().id, c);
        // Sin materia no hay tema posible.
        QVERIFY(st.crear(QString()).isEmpty());
    }

    void temas_elHistorialEsPorTemaNoPorMateria()
    {
        StudiaSessionStore st;
        const QString a = st.crear(QStringLiteral("Matemática 1"));
        const QString b = st.crear(QStringLiteral("Matemática 1"));
        auto msg = [](const QString &txt) {
            return QVariantMap{{QStringLiteral("rol"), QStringLiteral("usuario")},
                               {QStringLiteral("contenido"), txt}};
        };
        st.porId(a)->mensajes << msg(QStringLiteral("integrales"));
        st.porId(b)->mensajes << msg(QStringLiteral("ecuaciones"));
        // Es lo que evita que una repregunta en un tema arrastre el otro.
        QCOMPARE(st.ultimasPreguntas(a, 5),
                 QStringList({QStringLiteral("integrales")}));
        QCOMPARE(st.ultimasPreguntas(b, 5),
                 QStringList({QStringLiteral("ecuaciones")}));
    }

    void temas_ordenadosPorUsoReciente()
    {
        StudiaSessionStore st;
        const QString viejo = st.crear(QStringLiteral("M"));
        const QString nuevo = st.crear(QStringLiteral("M"));
        st.porId(viejo)->usada = 1000;
        st.porId(nuevo)->usada = 2000;
        QCOMPARE(st.deMateria(QStringLiteral("M")).first().id, nuevo);
        QCOMPARE(st.ultimoDe(QStringLiteral("M")), nuevo);
        QVERIFY(st.ultimoDe(QStringLiteral("sin temas")).isEmpty());
    }

    void temas_renombreManualGanaSobreElAutomatico()
    {
        StudiaSessionStore st;
        const QString id = st.crear(QStringLiteral("M"));
        QVERIFY(st.titular(id, QStringLiteral("Título automático")));
        QCOMPARE(st.porId(id)->titulo, QStringLiteral("Título automático"));
        QVERIFY(st.renombrar(id, QStringLiteral("Mi nombre")));
        // Ya tiene nombre propio: una respuesta nueva no lo pisa.
        QVERIFY(!st.titular(id, QStringLiteral("otra cosa")));
        QCOMPARE(st.porId(id)->titulo, QStringLiteral("Mi nombre"));
        // Un nombre vacío dejaría una fila sin texto en la lista.
        QVERIFY(!st.renombrar(id, QStringLiteral("   ")));
        QCOMPARE(st.porId(id)->titulo, QStringLiteral("Mi nombre"));
        QVERIFY(!st.renombrar(QStringLiteral("no-existe"), QStringLiteral("x")));
    }

    void temas_sinTituloSeQuedaConElNombrePorDefecto()
    {
        // Es el caso de la abstención: no se aprendió de qué trata el tema.
        StudiaSessionStore st;
        const QString id = st.crear(QStringLiteral("M"));
        QVERIFY(!st.titular(id, QString()));
        QVERIFY(!st.titular(id, QStringLiteral("   ")));
        QCOMPARE(st.porId(id)->titulo, StudiaSessionStore::tituloPorDefecto());
        QVERIFY(st.porId(id)->tituloAuto);   // sigue esperando un título
    }

    void temas_resumenPorMateriaAgrupaYOrdena()
    {
        StudiaSessionStore st;
        const QString a = st.crear(QStringLiteral("Matemática 1"));
        const QString b = st.crear(QStringLiteral("Matemática 1"));
        const QString c = st.crear(QStringLiteral("Redes"));
        st.porId(a)->usada = 1000;
        st.porId(a)->mensajes.append(QVariantMap{});
        st.porId(b)->usada = 3000;
        st.porId(b)->mensajes.append(QVariantMap{});
        st.porId(b)->mensajes.append(QVariantMap{});
        st.porId(c)->usada = 2000;

        const QVariantList r = st.resumenPorMateria();
        QCOMPARE(r.size(), 2);                       // una fila por materia
        const QVariantMap m0 = r.at(0).toMap();
        QCOMPARE(m0.value(QStringLiteral("materia")).toString(),
                 QStringLiteral("Matemática 1"));    // la más usada primero
        QCOMPARE(m0.value(QStringLiteral("temas")).toInt(), 2);
        QCOMPARE(m0.value(QStringLiteral("mensajes")).toInt(), 3);
        QCOMPARE(r.at(1).toMap().value(QStringLiteral("materia")).toString(),
                 QStringLiteral("Redes"));
    }

    void temas_borrarMateriaSacaTodosSusTemas()
    {
        StudiaSessionStore st;
        st.crear(QStringLiteral("M"));
        st.crear(QStringLiteral("M"));
        st.crear(QStringLiteral("Otra"));
        QCOMPARE(st.borrarMateria(QStringLiteral("M")), 2);
        QCOMPARE(st.cantidadDe(QStringLiteral("M")), 0);
        QCOMPARE(st.cantidadDe(QStringLiteral("Otra")), 1);
        QCOMPARE(st.borrarMateria(QStringLiteral("no existe")), 0);
    }

    void temas_limpiarVaciaYBorrarElimina()
    {
        StudiaSessionStore st;
        const QString id = st.crear(QStringLiteral("X"));
        st.porId(id)->mensajes.append(QVariantMap{});
        st.titular(id, QStringLiteral("algo"));
        QVERIFY(st.limpiar(id));
        QCOMPARE(st.cantidad(), 1);
        QVERIFY(st.porId(id)->mensajes.isEmpty());
        // Vaciarlo lo deja como recién creado, listo para titularse de nuevo.
        QCOMPARE(st.porId(id)->titulo, StudiaSessionStore::tituloPorDefecto());
        QVERIFY(st.borrar(id));
        QCOMPARE(st.cantidad(), 0);
        QVERIFY(!st.borrar(id));
        QVERIFY(st.porId(id) == nullptr);
    }

    void temas_persistenEnDisco()
    {
        QString id;
        {
            StudiaSessionStore st;
            st.cargar();
            id = st.crear(QStringLiteral("Persistida"));
            st.renombrar(id, QStringLiteral("Nombre a mano"));
            st.porId(id)->mensajes.append(
                QVariantMap{{QStringLiteral("rol"), QStringLiteral("usuario")},
                            {QStringLiteral("contenido"), QStringLiteral("dato")},
                            {QStringLiteral("escribiendo"), true}});
            st.guardar();
        }
        StudiaSessionStore otra;
        otra.cargar();
        const StudiaSesion *s = otra.porId(id);
        QVERIFY(s != nullptr);
        QCOMPARE(s->titulo, QStringLiteral("Nombre a mano"));
        QVERIFY(!s->tituloAuto);          // el renombre sobrevive al reinicio
        QCOMPARE(s->mensajes.size(), 1);
        // El flag de "escribiendo" no se persiste como true.
        QVERIFY(!s->mensajes.first().toMap().value(QStringLiteral("escribiendo")).toBool());
        otra.borrar(id);
        otra.guardar();
    }

    void temas_migranDelFormatoViejo()
    {
        // Formato anterior: una sesión por materia, titulada "StudIA: <materia>"
        // y sin el campo tituloAuto. Ese título no distingue nada cuando hay
        // varios temas, así que se reemplaza por la primera pregunta.
        const QString ruta = StudiaSessionStore().rutaArchivo();
        QFile f(ruta);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(R"([{"id":"viejo-1","materia":"Física 1",
                     "titulo":"StudIA: Física 1","creada":1,"usada":2,
                     "mensajes":[{"rol":"usuario","contenido":"qué es el momento de inercia"},
                                 {"rol":"asistente","contenido":"## Momento de inercia\nEs…"}]}])");
        f.close();

        StudiaSessionStore st;
        st.cargar();
        const StudiaSesion *s = st.porId(QStringLiteral("viejo-1"));
        QVERIFY(s != nullptr);
        QCOMPARE(s->materia, QStringLiteral("Física 1"));
        QCOMPARE(s->titulo, QStringLiteral("Momento de inercia"));
        QVERIFY(s->tituloAuto);
        QCOMPARE(s->mensajes.size(), 2);   // no se pierde la conversación
        QFile::remove(ruta);
    }

    void temas_migradoSinPreguntasQuedaConNombrePorDefecto()
    {
        const QString ruta = StudiaSessionStore().rutaArchivo();
        QFile f(ruta);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(R"([{"id":"v2","materia":"Química","titulo":"StudIA: Química",
                     "creada":1,"usada":2,"mensajes":[]}])");
        f.close();
        StudiaSessionStore st;
        st.cargar();
        QCOMPARE(st.porId(QStringLiteral("v2"))->titulo,
                 StudiaSessionStore::tituloPorDefecto());
        QFile::remove(ruta);
    }

    // ── Controlador: temas ──

    void controlador_noSePuedeAbrirTemaSinHaberPreguntado()
    {
        StudiaController c;
        QVERIFY(c.abrirIndice(m_db));
        c.setMateria(QStringLiteral("Sistemas de Control"));
        // Recién elegida la materia el chat está vacío: crear otro tema dejaría
        // dos sin nombre y sin contenido.
        QVERIFY(!c.puedeCrearTema());
        const QString antes = c.temaId();
        c.nuevoTema();
        QCOMPARE(c.temaId(), antes);            // no hizo nada
        QCOMPARE(c.temas().size(), 1);
    }

    void controlador_alPreguntarSeHabilitaTemas()
    {
        StudiaController c;
        QVERIFY(c.abrirIndice(m_db));
        c.setMateria(QStringLiteral("Sistemas de Control"));
        c.preguntar(QStringLiteral("criterio de estabilidad de Routh"));
        QVERIFY(c.puedeCrearTema());
    }

    void controlador_ankiSoloEnFlashcardsNoEnAutoevaluacion()
    {
        // Las dos tienen la misma forma: preguntas numeradas, la línea, y
        // respuestas numeradas. Pero una autoevaluación no son tarjetas de
        // repaso y ofrecer exportarla a Anki sería un error.
        StudiaController c;
        const QString r = QStringLiteral("1. Una pregunta.\n2. Otra pregunta.\n\n")
            + StudiaPrompt::separadorRespuestas()
            + QStringLiteral("\n\n1. Una respuesta.\n2. Otra respuesta.");
        QCOMPARE(c.contarFlashcards(r, QStringLiteral("flashcards")), 2);
        QCOMPARE(c.contarFlashcards(r, QStringLiteral("autoevaluacion")), 0);
        QCOMPARE(c.contarFlashcards(r, QStringLiteral("resumen")), 0);
        // Sin modo (mensajes viejos) se cuenta igual: no se pierde nada.
        QCOMPARE(c.contarFlashcards(r), 2);
    }

    void controlador_revelarRespuestasNoMandaNadaAlModelo()
    {
        StudiaController c;
        QVERIFY(c.abrirIndice(m_db));
        c.setMateria(QStringLiteral("Sistemas de Control"));
        QVERIFY(c.modoOcultaRespuestas(QStringLiteral("autoevaluacion")));
        QVERIFY(c.modoOcultaRespuestas(QStringLiteral("flashcards")));
        QVERIFY(!c.modoOcultaRespuestas(QStringLiteral("resumen")));

        c.preguntar(QStringLiteral("/autoevaluacion/ criterio de estabilidad de Routh"));
        const int antes = c.mensajes().size();
        // Desplegar es un cambio de vista: no agrega mensajes ni consulta al
        // modelo. Un índice inválido tampoco puede romper nada.
        c.revelarRespuestas(antes - 1);
        c.revelarRespuestas(-1);
        c.revelarRespuestas(9999);
        QCOMPARE(c.mensajes().size(), antes);
        c.detener();
    }

    void controlador_siSeAbstieneElTemaSigueSinNombre()
    {
        // Sin material no se aprendió de qué trata el tema: ponerle de nombre
        // la frase de abstención sería peor que dejarlo sin nombre.
        StudiaController c;
        QVERIFY(c.abrirIndice(m_db));
        c.setMateria(QStringLiteral("Sistemas de Control"));
        c.preguntar(QStringLiteral("recetas de cocina peruana"));
        QCOMPARE(c.mensajes().size(), 2);          // pregunta + abstención
        QCOMPARE(c.tituloTema(), StudiaSessionStore::tituloPorDefecto());
    }

    void controlador_temasDeLaMateriaSonIndependientes()
    {
        StudiaController c;
        QVERIFY(c.abrirIndice(m_db));
        c.setMateria(QStringLiteral("Sistemas de Control"));
        c.preguntar(QStringLiteral("criterio de estabilidad de Routh"));
        const QString primero = c.temaId();
        const int mensajesPrimero = c.mensajes().size();
        QVERIFY(mensajesPrimero > 0);

        c.nuevoTema();
        QVERIFY(c.temaId() != primero);
        QVERIFY(c.mensajes().isEmpty());        // arranca limpio
        QCOMPARE(c.temas().size(), 2);

        // Volver al primero devuelve su conversación intacta.
        c.abrirTema(primero);
        QCOMPARE(c.temaId(), primero);
        QCOMPARE(c.mensajes().size(), mensajesPrimero);
    }

    void controlador_borrarElTemaAbiertoDejaOtroEnPie()
    {
        StudiaController c;
        QVERIFY(c.abrirIndice(m_db));
        c.setMateria(QStringLiteral("Sistemas de Control"));
        c.preguntar(QStringLiteral("criterio de estabilidad de Routh"));
        const QString unico = c.temaId();
        c.borrarTema(unico);
        // La materia sigue elegida y hay un tema nuevo y vacío: la página nunca
        // queda sin chat.
        QVERIFY(c.materiaElegida());
        QVERIFY(c.temaId() != unico);
        QVERIFY(!c.temaId().isEmpty());
        QVERIFY(c.mensajes().isEmpty());
    }

    void controlador_renombrarTema()
    {
        StudiaController c;
        QVERIFY(c.abrirIndice(m_db));
        c.setMateria(QStringLiteral("Sistemas de Control"));
        c.preguntar(QStringLiteral("criterio de estabilidad de Routh"));
        QVERIFY(c.renombrarTema(c.temaId(), QStringLiteral("Estabilidad")));
        QCOMPARE(c.tituloTema(), QStringLiteral("Estabilidad"));
        QVERIFY(!c.renombrarTema(c.temaId(), QString()));
    }

    // ── Controlador: camino sin red ──

    void controlador_sinMateriaNoPregunta()
    {
        StudiaController c;
        QVERIFY(c.abrirIndice(m_db));
        c.setMateria(QString());
        QVERIFY(!c.materiaElegida());
        QSignalSpy spy(&c, &StudiaController::errorOcurrido);
        c.preguntar(QStringLiteral("¿Qué es Modbus?"));
        QCOMPARE(spy.count(), 1);          // avisa que falta elegir materia
        QVERIFY(c.mensajes().isEmpty());   // no se escribió nada
        QVERIFY(!c.generando());
    }

    void controlador_cambiarMateriaCambiaDeConversacion()
    {
        StudiaController c;
        QVERIFY(c.abrirIndice(m_db));
        c.setMateria(QStringLiteral("Sistemas de Control"));
        // Materia recién elegida: un tema vacío, todavía sin nombre propio.
        QCOMPARE(c.tituloTema(), StudiaSessionStore::tituloPorDefecto());
        c.preguntar(QStringLiteral("recetas de cocina peruana"));   // se abstiene
        QCOMPARE(c.mensajes().size(), 2);

        // Otra materia → otra conversación, vacía.
        c.setMateria(QStringLiteral("Redes Industriales"));
        QVERIFY(c.mensajes().isEmpty());

        // Al volver, el historial de la primera sigue ahí.
        c.setMateria(QStringLiteral("Sistemas de Control"));
        QCOMPARE(c.mensajes().size(), 2);
    }

    void controlador_elPanelListaMateriasNoTemas()
    {
        StudiaController c;
        QVERIFY(c.abrirIndice(m_db));
        c.setMateria(QStringLiteral("Sistemas de Control"));
        c.preguntar(QStringLiteral("criterio de estabilidad de Routh"));
        c.nuevoTema();
        c.preguntar(QStringLiteral("polos y ceros"));
        // Dos temas, pero UNA sola fila en el panel: navega entre materias.
        QCOMPARE(c.temas().size(), 2);
        QCOMPARE(c.materiasConChats().size(), 1);
        QCOMPARE(c.materiasConChats().first().toMap()
                     .value(QStringLiteral("temas")).toInt(), 2);
    }

    void controlador_borrarLosChatsDeUnaMateriaSueltaLaMateria()
    {
        StudiaController c;
        QVERIFY(c.abrirIndice(m_db));
        c.setMateria(QStringLiteral("Sistemas de Control"));
        c.preguntar(QStringLiteral("criterio de estabilidad de Routh"));
        c.nuevoTema();
        c.borrarChatsDeMateria(QStringLiteral("Sistemas de Control"));
        // Si la materia quedara elegida, sesionActual() le crearía un tema y el
        // chat reaparecería como si el borrado no hubiera funcionado.
        QVERIFY(!c.materiaElegida());
        QVERIFY(c.materiasConChats().isEmpty());
        QVERIFY(c.mensajes().isEmpty());
    }

    void controlador_seAbstieneSinFragmentos()
    {
        StudiaController c;
        QVERIFY(c.abrirIndice(m_db));
        c.setMateria(QStringLiteral("Sistemas de Control"));
        c.limpiar();
        // Tema inexistente en el corpus: se responde la abstencion SIN red.
        c.preguntar(QStringLiteral("recetas de cocina peruana"));
        QCOMPARE(c.mensajes().size(), 2);
        QVERIFY(!c.generando());
        QCOMPARE(c.mensajes().at(1).toMap().value(QStringLiteral("contenido")).toString(),
                 StudiaController::fraseAbstencion());
        QVERIFY(c.mensajes().at(1).toMap().value(QStringLiteral("fuentes")).toList().isEmpty());
        c.limpiar();
    }

    void controlador_conFragmentosPeroSinServidorReportaError()
    {
        StudiaController c;
        QVERIFY(c.abrirIndice(m_db));
        c.setUmbralAbstencion(0.0);
        c.setMateria(QStringLiteral("Sistemas de Control"));
        c.limpiar();
        c.setServerUrl(QString());        // no hay servidor
        c.preguntar(QStringLiteral("criterio de Routh"));
        QCOMPARE(c.mensajes().size(), 2);
        const QVariantMap resp = c.mensajes().at(1).toMap();
        // Recupero fuentes (la busqueda funciono) pero no pudo generar.
        QVERIFY(resp.value(QStringLiteral("contenido")).toString().startsWith(
            QStringLiteral("[error")));
        QVERIFY(!c.generando());
        c.limpiar();
    }

    void controlador_guardaElModoEnElMensaje()
    {
        StudiaController c;
        QVERIFY(c.abrirIndice(m_db));
        c.setMateria(QStringLiteral("Sistemas de Control"));
        c.limpiar();
        c.preguntar(QStringLiteral("/flashcards/ recetas de cocina peruana"));
        QVERIFY(!c.mensajes().isEmpty());
        const QVariantMap u = c.mensajes().at(0).toMap();
        QCOMPARE(u.value(QStringLiteral("modo")).toString(), QStringLiteral("flashcards"));
        // El prefijo NO queda dentro del texto de la pregunta.
        QCOMPARE(u.value(QStringLiteral("contenido")).toString(),
                 QStringLiteral("recetas de cocina peruana"));
        c.limpiar();
    }

    void controlador_prefijoDeModo()
    {
        StudiaController c;
        QCOMPARE(c.prefijoDeModo(QStringLiteral("flashcards")), QStringLiteral("/flashcards/ "));
        QVERIFY(c.prefijoDeModo(StudiaPrompt::idModoLibre()).isEmpty());
        QVERIFY(c.prefijoDeModo(QStringLiteral("inexistente")).isEmpty());
    }

    void controlador_modoInvalidoCaeALibre()
    {
        StudiaController c;
        c.setModo(QStringLiteral("no-existe"));
        QCOMPARE(c.modo(), StudiaPrompt::idModoLibre());
        c.setModo(QStringLiteral("resumen"));
        QCOMPARE(c.modo(), QStringLiteral("resumen"));
    }

    void controlador_ignoraPreguntaVacia()
    {
        StudiaController c;
        QVERIFY(c.abrirIndice(m_db));
        c.setMateria(QStringLiteral("Sistemas de Control"));
        c.limpiar();
        c.preguntar(QStringLiteral("   "));
        c.preguntar(QStringLiteral("/flashcards/   "));   // modo sin texto
        QVERIFY(c.mensajes().isEmpty());
    }

    void controlador_limpiaLaConversacion()
    {
        StudiaController c;
        QVERIFY(c.abrirIndice(m_db));
        c.setMateria(QStringLiteral("Sistemas de Control"));
        c.preguntar(QStringLiteral("recetas de cocina peruana"));
        QVERIFY(!c.mensajes().isEmpty());
        c.limpiar();
        QVERIFY(c.mensajes().isEmpty());
    }

    void controlador_buscarExponeMetadatosAQml()
    {
        StudiaController c;
        QVERIFY(c.abrirIndice(m_db));
        c.setUmbralAbstencion(0.0);
        c.setMateria(QStringLiteral("Redes Industriales"));
        const QVariantList r = c.buscar(QStringLiteral("Modbus TCP"), 3);
        QVERIFY(!r.isEmpty());
        const QVariantMap f = r.first().toMap();
        QCOMPARE(f.value(QStringLiteral("documento")).toString(), QStringLiteral("modbus.pdf"));
        QVERIFY(f.contains(QStringLiteral("materia")));
        QVERIFY(f.contains(QStringLiteral("pagina")));
        QVERIFY(f.contains(QStringLiteral("ruta")));
        QVERIFY(f.contains(QStringLiteral("texto")));
    }

    void controlador_laMateriaActivaAcotaLaBusqueda()
    {
        StudiaController c;
        QVERIFY(c.abrirIndice(m_db));
        c.setUmbralAbstencion(0.0);
        c.setMateria(QStringLiteral("Redes Industriales"));
        const QVariantList r = c.buscar(QStringLiteral("estabilidad Modbus red"), 5);
        QVERIFY(!r.isEmpty());
        for (const QVariant &v : r)
            QCOMPARE(v.toMap().value(QStringLiteral("materia")).toString(),
                     QStringLiteral("Redes Industriales"));
    }

    void controlador_kSeAcota()
    {
        StudiaController c;
        c.setFragmentosK(999);
        QCOMPARE(c.fragmentosK(), 15);
        c.setFragmentosK(0);
        QCOMPARE(c.fragmentosK(), 2);
    }
};

QTEST_MAIN(TestStudia)
#include "test_studia.moc"
