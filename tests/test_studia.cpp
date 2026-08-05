#include <QtTest>
#include <QDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "core/studia/StudiaController.h"
#include "core/studia/StudiaIndex.h"

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
        QVERIFY(m_dir.isValid());
        m_db = m_dir.filePath(QStringLiteral("studia_test.db"));
        QVERIFY2(construirIndice(m_db), "no se pudo construir el indice de prueba");
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

        const QString p = StudiaController::construirPrompt(
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
        const QString p = StudiaController::construirPrompt(QStringLiteral("hola"), frags);
        // El cuerpo se acota; el prompt no puede arrastrar los 5000 caracteres.
        QVERIFY(p.size() < 3000);
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
        const QString s = StudiaController::promptSistema();
        QVERIFY(s.contains(StudiaController::fraseAbstencion()));
        QVERIFY(s.contains(QStringLiteral("Mecatrónica")));
        QVERIFY(s.contains(QStringLiteral("[n]")));
    }

    // ── Controlador: camino sin red ──

    void controlador_sinIndiceAvisaYNoLlamaAlModelo()
    {
        StudiaController c;
        c.limpiar();
        QSignalSpy spy(&c, &StudiaController::mensajesChanged);
        c.preguntar(QStringLiteral("¿Qué es Modbus?"));
        QVERIFY(spy.count() >= 2);            // pregunta + respuesta
        QCOMPARE(c.mensajes().size(), 2);
        QVERIFY(!c.generando());              // nunca se abrio una conexion
        const QVariantMap resp = c.mensajes().at(1).toMap();
        QCOMPARE(resp.value(QStringLiteral("rol")).toString(), QStringLiteral("asistente"));
        QVERIFY(resp.value(QStringLiteral("contenido")).toString().contains(
            QStringLiteral("índice")));
    }

    void controlador_seAbstieneSinFragmentos()
    {
        StudiaController c;
        QVERIFY(c.abrirIndice(m_db));
        c.limpiar();
        // Tema inexistente en el corpus: se responde la abstencion SIN red.
        c.preguntar(QStringLiteral("recetas de cocina peruana"));
        QCOMPARE(c.mensajes().size(), 2);
        QVERIFY(!c.generando());
        QCOMPARE(c.mensajes().at(1).toMap().value(QStringLiteral("contenido")).toString(),
                 StudiaController::fraseAbstencion());
        QVERIFY(c.mensajes().at(1).toMap().value(QStringLiteral("fuentes")).toList().isEmpty());
    }

    void controlador_conFragmentosPeroSinServidorReportaError()
    {
        StudiaController c;
        QVERIFY(c.abrirIndice(m_db));
        c.setUmbralAbstencion(0.0);
        c.limpiar();
        c.setServerUrl(QString());        // no hay servidor
        c.preguntar(QStringLiteral("criterio de Routh"));
        QCOMPARE(c.mensajes().size(), 2);
        const QVariantMap resp = c.mensajes().at(1).toMap();
        // Recupero fuentes (la busqueda funciono) pero no pudo generar.
        QVERIFY(resp.value(QStringLiteral("contenido")).toString().startsWith(
            QStringLiteral("[error")));
        QVERIFY(!c.generando());
    }

    void controlador_ignoraPreguntaVacia()
    {
        StudiaController c;
        QVERIFY(c.abrirIndice(m_db));
        c.limpiar();
        c.preguntar(QStringLiteral("   "));
        QVERIFY(c.mensajes().isEmpty());
    }

    void controlador_limpiaLaConversacion()
    {
        StudiaController c;
        QVERIFY(c.abrirIndice(m_db));
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
        const QVariantList r = c.buscar(QStringLiteral("Modbus TCP"), 3);
        QVERIFY(!r.isEmpty());
        const QVariantMap f = r.first().toMap();
        QCOMPARE(f.value(QStringLiteral("documento")).toString(), QStringLiteral("modbus.pdf"));
        QVERIFY(f.contains(QStringLiteral("materia")));
        QVERIFY(f.contains(QStringLiteral("pagina")));
        QVERIFY(f.contains(QStringLiteral("ruta")));
        QVERIFY(f.contains(QStringLiteral("texto")));
    }

    void controlador_filtroDeMateriaSeAplica()
    {
        StudiaController c;
        QVERIFY(c.abrirIndice(m_db));
        c.setUmbralAbstencion(0.0);
        c.setMateriaFiltro(QStringLiteral("Redes Industriales"));
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
