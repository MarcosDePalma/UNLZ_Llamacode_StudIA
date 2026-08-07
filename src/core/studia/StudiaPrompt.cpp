#include "StudiaPrompt.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QVariantMap>

namespace {

// Cuanto texto de cada fragmento se manda al modelo. Los fragmentos son de
// ~1200 caracteres; con 6 fragmentos son ~7 KB ≈ 1800 tokens, holgado dentro
// de los 32k de contexto del perfil Qwen y deja lugar para la respuesta.
constexpr int kMaxCharsFragmento = 1400;

// Reglas de formato: valen para todos los modos. El chat renderiza Markdown,
// asi que se le pide markdown y se le prohibe LaTeX (Qt no lo dibuja y al
// estudiante le llega ruido tipo \frac{a}{b}).
QString reglasDeFormato()
{
    return QStringLiteral(
        "FORMATO DE LA RESPUESTA:\n"
        "- Escribí en Markdown: `## Título` para las secciones, `**negrita**` para "
        "los términos clave, `-` para listas y `1.` para pasos numerados.\n"
        "- NO uses LaTeX ni $...$. Las fórmulas van en texto plano legible: "
        "escribí `V = I · R`, `n = 120·f/p`, `x²` (no `x^2` ni `\\frac{a}{b}`; "
        "para una división usá `a/b` o `(a+b)/c`).\n"
        "- Usá `código` sólo para nombres de archivo, comandos o identificadores.\n"
        "- No escribas párrafos de más de 5 o 6 líneas: cortá con subtítulos.");
}

// Los dos tipos de imagen que la app sabe dibujar. Se le explica al modelo el
// formato exacto porque un 7B no lo adivina.
QString reglasDeImagenes()
{
    return QStringLiteral(
        "IMÁGENES: la aplicación dibuja figuras por vos. Si el estudiante te pide "
        "«graficá», «dibujá», «mostrame la curva», «hacé un diagrama» o "
        "«esquematizá», tu respuesta DEBE incluir el bloque correspondiente. NO "
        "expliques cómo graficarlo ni le digas que use otra herramienta: emitilo "
        "y listo. Fuera de esos pedidos, usalas sólo cuando ayuden de verdad.\n\n"
        "Los formatos de abajo son PLANTILLAS con marcadores entre < >. Nunca "
        "las copies tal cual: completalas con lo que pidió el estudiante. Si no "
        "te pidió una figura, no emitas ninguna.\n\n"
        "1) GRÁFICO DE FUNCIONES — para ver una curva, comparar dos o mostrar el "
        "área de una integral:\n"
        "```grafico\n"
        "funcion: <expresión en función de x>\n"
        "rango: <x mínimo>, <x máximo>\n"
        "area: <desde>, <hasta>\n"
        "titulo: <título>\n"
        "```\n"
        "La expresión es Python/numpy sobre `x` (`**` para potencia, `sqrt`, "
        "`sin`, `cos`, `exp`, `log`, `pi`). `rango` es obligatorio; `area` es "
        "opcional y sombrea bajo la primera curva. Podés repetir `funcion:` para "
        "superponer curvas. Nada de otro código.\n\n"
        "2) DIAGRAMA — para procesos, clasificaciones o relaciones entre "
        "conceptos:\n"
        "```mermaid\n"
        "flowchart TD\n"
        "  <id1>[<etiqueta>] --> <id2>{<pregunta>}\n"
        "  <id2> -- sí --> <id3>[<etiqueta>]\n"
        "  <id2> -- no --> <id4>[<etiqueta>]\n"
        "```\n"
        "Etiquetas cortas y sin caracteres raros.");
}

}  // namespace


namespace StudiaPrompt {

QString idModoLibre() { return QStringLiteral("libre"); }

QString fraseAbstencion()
{
    return QStringLiteral("No encontré información suficiente en la documentación "
                          "disponible para responder con seguridad.");
}

QVector<Modo> modos()
{
    static const QVector<Modo> v = {
        // Flexibles: conversar y entender. Exigentes: producir material de
        // estudio que se va a usar como si fuera fiel al apunte.
        // El modo libre no lleva color: es la conversación normal y usa el
        // color estándar de la app. Los colores distinguen a los OTROS.
        {QStringLiteral("libre"), QStringLiteral("Conversación"),
         QStringLiteral("Preguntas y respuestas sobre el material."),
         QString(), QString(), false, false},

        {QStringLiteral("resumen"), QStringLiteral("Resumen"),
         QStringLiteral("Condensa el tema en sus ideas principales."),
         QStringLiteral(
             "MODO RESUMEN. El estudiante ya leyó —o va a leer— un material "
             "largo y quiere quedarse con lo que importa. No le expliques: "
             "condensá.\n\n"
             "ESCRIBÍ EN PROSA CORRIDA, en 2 a 4 párrafos densos. Nada de "
             "títulos, subtítulos ni viñetas: un resumen partido en secciones "
             "con encabezados es una ficha, no un resumen, y se lee peor. Los "
             "párrafos se encadenan: cada uno retoma el anterior.\n\n"
             "La única excepción son las FÓRMULAS, que van en su propio renglón "
             "con el significado de cada símbolo al lado.\n\n"
             "Conservá sí o sí: definiciones, fórmulas, clasificaciones "
             "completas y las relaciones entre conceptos (qué depende de qué, "
             "qué se opone a qué, qué es un caso particular de qué). Sacá: "
             "ejemplos, rodeos, repeticiones y todo lo accesorio.\n\n"
             "Ojo con la diferencia: si te preguntan algo puntual, no estás "
             "respondiendo esa pregunta — estás condensando TODO el material "
             "sobre ese tema. Y resumir es elegir, no agregar: no expliques con "
             "palabras tuyas lo que el material no dice ni saques conclusiones "
             "propias. Si un tema aparece incompleto, resumí lo que hay y aclaralo "
             "en una frase al final."),
         QStringLiteral("#3B82F6"), true, false},

        {QStringLiteral("explicacion"), QStringLiteral("Explicación"),
         QStringLiteral("Desarrolla el concepto de menor a mayor dificultad."),
         QStringLiteral(
             "MODO EXPLICACIÓN. El estudiante no entendió algo y necesita que "
             "se lo expliquen. Sos paciente y vas de menor a mayor: nunca "
             "arrancás por lo técnico.\n\n"
             "1. `## La idea` — la intuición en lenguaje cotidiano, sin una sola "
             "fórmula ni tecnicismo. Si sirve una analogía, usala y aclarala "
             "como analogía.\n"
             "2. `## Cómo funciona` — recién acá el desarrollo técnico, paso a "
             "paso, introduciendo cada término la primera vez que aparece.\n"
             "3. `## Un ejemplo` — un caso concreto y numérico si se puede, "
             "tomado de la documentación. Si los fragmentos no traen ninguno, "
             "decilo en vez de inventarlo.\n"
             "4. `## Con qué se conecta` — dónde encaja esto: qué concepto "
             "anterior hace falta, para qué se usa después, con qué suele "
             "confundirse.\n\n"
             "Ajustate a lo que muestra el estudiante: si su pregunta es básica "
             "empezá de cero; si ya maneja el tema, no le repitas lo obvio. Si "
             "vuelve a preguntar sobre lo mismo, no repitas la explicación con "
             "otras palabras: cambiá el enfoque o el ejemplo."),
         QStringLiteral("#EAB308"), false, false},

        {QStringLiteral("autoevaluacion"), QStringLiteral("Autoevaluación"),
         QStringLiteral("Te toma examen para ver qué sabés de verdad."),
         QStringLiteral(
             "MODO AUTOEVALUACIÓN. Le tomás examen al estudiante para que "
             "descubra qué no sabe. Diez preguntas con su respuesta, en pares "
             "«P:» y «R:».\n"
             "Que pidan explicar, aplicar o justificar; no repetir "
             "definiciones. De la más fácil a la más difícil.\n"
             "No preguntes nada que no puedas responder con los fragmentos."),
         QStringLiteral("#8B5CF6"), true, true},

        {QStringLiteral("flashcards"), QStringLiteral("Flashcards"),
         QStringLiteral("Tarjetas de repaso para memorizar."),
         QStringLiteral(
             "MODO FLASHCARDS. Tarjetas para repasar un tema ya visto: cinco "
             "pares «P:» y «R:», cortos, que se leen de un vistazo.\n"
             "Preguntá lo que hay que saberse de memoria: definiciones, "
             "fórmulas, unidades, clasificaciones. Una idea por tarjeta.\n"
             "No escribas «Frente» ni «Dorso»."),
         QStringLiteral("#F97316"), true, true},

        {QStringLiteral("ejercitacion"), QStringLiteral("Ejercitación"),
         QStringLiteral("Ejercicios para practicar, de menor a mayor dificultad."),
         QStringLiteral(
             "MODO EJERCITACIÓN. El estudiante quiere PRACTICAR y aprender a "
             "resolver, no que le resuelvan. Enseñás el procedimiento, no "
             "entregás resultados.\n\n"
             "Si TRAE UN PROBLEMA suyo, resolvelo mostrando el camino:\n"
             "1. `## Datos` — qué se conoce y qué se pide, con unidades.\n"
             "2. `## Método` — qué procedimiento de la documentación aplica y "
             "por qué, citando `[n]`. La fórmula sale de los fragmentos: si no "
             "está, decilo y no la inventes.\n"
             "3. `## Desarrollo` — un paso por renglón, mostrando el reemplazo "
             "de valores antes de operar y cada resultado intermedio.\n"
             "4. `## Resultado` — el valor final con su unidad.\n"
             "5. `## Verificación` — un chequeo rápido: orden de magnitud, "
             "coherencia de unidades o caso límite.\n\n"
             "Si PIDE EJERCICIOS para practicar, armá una serie progresiva:\n"
             "- `### Básico` — aplicación directa de una fórmula o definición.\n"
             "- `### Intermedio` — combina dos ideas o exige despejar.\n"
             "- `### Avanzado` — problema con contexto, donde primero hay que "
             "decidir qué método usar.\n"
             "Cada ejercicio con sus datos completos y, debajo, una `Pista:` de "
             "un renglón que oriente sin resolver. Las soluciones NO van salvo "
             "que las pida: primero que lo intente.\n\n"
             "Estás aplicando métodos del apunte a números nuevos: eso es "
             "correcto y es lo que se te pide. Lo que no podés es inventar la "
             "fórmula ni los datos. Si hacés una cuenta de la que no estás "
             "seguro, marcala como 'verificar a mano'."),
         QStringLiteral("#EF4444"), false, false},

        {QStringLiteral("plan"), QStringLiteral("Plan de estudio"),
         QStringLiteral("Organiza qué estudiar, cuándo y en qué orden."),
         QStringLiteral(
             "MODO PLAN DE ESTUDIO. Todavía no sabés de cuánto tiempo dispone "
             "el estudiante, así que NO armes ningún plan: sin esos datos "
             "sería inventado. Copiá esta forma exacta:\n\n"
             "## Temas que abarca\n"
             "- (subtema): (qué entra, una línea)\n"
             "- (subtema): (qué entra, una línea)\n"
             "…los que salgan del material\n\n"
             "## Para armártelo necesito saber\n"
             "- Cuánto te cuesta cada tema de arriba (fácil / normal / difícil)\n"
             "- Cuántas horas por semana podés dedicarle\n"
             "- Para qué fecha lo necesitás\n\n"
             "Nada de sesiones, tablas ni cronogramas todavía."),
         QStringLiteral("#22C55E"), true, false},
    };
    return v;
}

QVariantList modosParaQml()
{
    QVariantList out;
    for (const Modo &m : modos()) {
        out.append(QVariantMap{
            {QStringLiteral("id"), m.id},
            {QStringLiteral("etiqueta"), m.etiqueta},
            {QStringLiteral("descripcion"), m.descripcion},
            {QStringLiteral("color"), m.color},
            {QStringLiteral("ocultaRespuestas"), m.ocultaRespuestas},
        });
    }
    return out;
}

QString separadorRespuestas()
{
    return QStringLiteral("--------RESPUESTAS--------");
}

namespace {

// Nombres alternativos que llegan al mismo modo.
//
// "ejercicio" era el id anterior de Ejercitación: hay chats guardados con ese
// valor y no se pueden quedar sin etiqueta ni color. Las variantes con tilde
// son las que el estudiante escribe a mano en el prefijo /.../.
QString idCanonico(const QString &id)
{
    static const QHash<QString, QString> alias = {
        {QStringLiteral("ejercicio"),     QStringLiteral("ejercitacion")},
        {QStringLiteral("ejercicios"),    QStringLiteral("ejercitacion")},
        {QStringLiteral("ejercitación"),  QStringLiteral("ejercitacion")},
        {QStringLiteral("explicación"),   QStringLiteral("explicacion")},
        {QStringLiteral("autoevaluación"),QStringLiteral("autoevaluacion")},
        {QStringLiteral("resúmen"),       QStringLiteral("resumen")},
    };
    const QString k = id.trimmed().toLower();
    return alias.value(k, k);
}

}  // namespace

Modo modoPorId(const QString &id)
{
    const QString buscado = idCanonico(id);
    for (const Modo &m : modos())
        if (m.id == buscado)
            return m;
    return {};
}

void separarModo(const QString &entrada, QString *idModo, QString *texto)
{
    const QString libre = idModoLibre();
    if (idModo) *idModo = libre;
    if (texto)  *texto  = entrada.trimmed();

    // /modo/ resto  → el id va entre barras, al principio.
    static const QRegularExpression rx(
        QStringLiteral("^\\s*/\\s*([A-Za-zÁÉÍÓÚáéíóúÑñ_-]+)\\s*/\\s*(.*)$"),
        QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpressionMatch m = rx.match(entrada);
    if (!m.hasMatch())
        return;
    const Modo modo = modoPorId(m.captured(1));
    if (modo.id.isEmpty())
        return;                       // prefijo desconocido: se trata como texto
    // Se devuelve el id canónico: /ejercicio/ y /ejercitación/ entran al mismo.
    if (idModo) *idModo = modo.id;
    if (texto)  *texto  = m.captured(2).trimmed();
}

QString instruccionPlanConDatos()
{
    return QStringLiteral(
        "MODO PLAN DE ESTUDIO. El estudiante ya te pasó cuánto tiempo tiene, "
        "para cuándo y qué le cuesta: está en la conversación de arriba. Armá "
        "el plan con ESOS datos, no con un cronograma genérico.\n\n"
        "Dos cosas mandan sobre todo lo demás:\n"
        "- El plan dura lo que él te dijo que tiene. Ni un día más.\n"
        "- Los temas que le cuestan, y los que ocupan más páginas del "
        "material, se llevan más horas que el resto.\n\n"
        "Si te pide un plan de otro tema y no tenés sus datos para ese, "
        "pedíselos igual que la primera vez.");
}

QString recordatorioDeFormato(const QString &idModo, bool planConDatos)
{
    const QString id = modoPorId(idModo).id;
    if (id == QLatin1String("autoevaluacion")) {
        return QStringLiteral(
            "### Cómo tiene que salir tu respuesta\n\n"
            "1. P: (la pregunta)\n"
            "   R: (la respuesta, con la cita [n])\n"
            "2. P: (la pregunta)\n"
            "   R: (la respuesta, con la cita [n])\n"
            "…así hasta la 10\n\n"
            "Diez pares, siempre. Cada uno con su P: y su R:, sin saltearte "
            "ninguna. Nada antes del 1 ni después del 10: ni títulos, ni "
            "resúmenes del tema, ni comentarios.");
    }
    if (id == QLatin1String("flashcards")) {
        return QStringLiteral(
            "### Cómo tiene que salir tu respuesta\n\n"
            "1. P: (pregunta corta)\n"
            "   R: (respuesta, una frase)\n"
            "2. P: (pregunta corta)\n"
            "   R: (respuesta, una frase)\n"
            "3. P: (pregunta corta)\n"
            "   R: (respuesta, una frase)\n"
            "4. P: (pregunta corta)\n"
            "   R: (respuesta, una frase)\n"
            "5. P: (pregunta corta)\n"
            "   R: (respuesta, una frase)\n\n"
            "Cinco pares, ni más ni menos. Cada respuesta, una frase de hasta "
            "20 palabras. Nada antes del 1 ni después del 5: ni títulos, ni "
            "explicación del tema.");
    }
    if (id == QLatin1String("plan")) {
        if (planConDatos) {
            // Las dos primeras secciones son las que hacen que el plan salga
            // bien. Sin ellas el modelo escribe la tabla de memoria: le da las
            // mismas horas a todos los temas e inventa días que el estudiante
            // no tiene. Obligarlo a escribir las cuentas ANTES lo ata a los
            // datos que le dieron.
            return QStringLiteral(
                "### Cómo tiene que salir tu respuesta\n\n"
                "## Lo que me dijiste\n"
                "- Tiempo: (los días y las horas que te dijo, tal cual)\n"
                "- Fecha: (la que te dijo)\n"
                "- Dificultad: (tema por tema, como te la dio)\n"
                "- Horas totales: (días × horas por día = N horas)\n\n"
                "## Reparto\n"
                "(una línea por tema: «tema — dificultad — X h»)\n"
                "Las X suman exactamente N. Un tema difícil se lleva el DOBLE "
                "de horas que uno fácil.\n\n"
                "## Sesiones\n"
                "| Día | Duración | Qué estudiar | Material | Cómo verificar |\n\n"
                "## Antes del examen\n(3 o 4 viñetas)\n\n"
                "UNA FILA POR DÍA, ni una más. Si te dijo que tiene 3 días, la "
                "tabla tiene exactamente 3 filas: Día 1, Día 2 y Día 3. No "
                "existe el día 4, ni para repasar. Dos filas para el mismo día "
                "tampoco: el día ya tiene sus horas y no le caben más.\n\n"
                "La duración de cada fila son las horas que tiene por día, "
                "siempre la misma.\n\n"
                "EN «QUÉ ESTUDIAR» SE VE EL REPARTO. Un tema al que le diste 4 h "
                "con 2 h por día ocupa DOS días enteros; dos temas de 1 h "
                "comparten un día. Si a cada día le ponés un tema distinto sin "
                "mirar el reparto, el plan no sirve: era justo lo que el "
                "estudiante pidió que tuvieras en cuenta.\n\n"
                "El repaso entra dentro de esos días, no después. Si el tema no "
                "entra en el tiempo que tiene, decí qué dejás afuera.");
        }
        return QStringLiteral(
            "### Cómo tiene que salir tu respuesta\n\n"
            "## Temas que abarca\n"
            "- (tema): (qué entra)\n"
            "…los que salgan del material\n\n"
            "## Para armártelo necesito saber\n"
            "- Cuánto te cuesta cada tema de arriba\n"
            "- Cuántas horas por semana podés dedicarle\n"
            "- Para qué fecha lo necesitás\n\n"
            "Sólo esas dos secciones. NO armes el plan todavía: sin esos datos "
            "sería inventado. Nada de sesiones, tablas ni cronogramas.");
    }
    return QString();
}

QString sistema(const QString &materia, const QString &idModo, bool planConDatos)
{
    QString s = QStringLiteral(
        "Sos StudIA, un asistente de estudio para estudiantes de Ingeniería "
        "Mecatrónica de la Universidad Nacional de Lomas de Zamora.\n\n");

    if (!materia.trimmed().isEmpty()) {
        s += QStringLiteral(
            "Esta conversación es sobre la materia **%1**. Todo lo que respondas "
            "sale de la documentación de esa materia; no mezcles contenido de "
            "otras.\n\n").arg(materia.trimmed());
    }

    s += QStringLiteral(
        "Respondés APOYÁNDOTE ÚNICAMENTE en los fragmentos de documentación "
        "académica que te paso en cada consulta. Esos fragmentos salen de los "
        "apuntes, libros y trabajos prácticos de la carrera.\n\n"
        "REGLAS:\n"
        "1. Si los fragmentos no alcanzan para responder con seguridad, "
        "respondé exactamente esta frase y nada más: \"%1\"\n"
        "2. No completes con conocimiento general lo que no esté en los "
        "fragmentos. Si sabés algo por tu cuenta pero no está en la "
        "documentación, no lo afirmes.\n"
        "2b. Lo que SÍ podés hacer es RAZONAR sobre los fragmentos: aplicar un "
        "método a un caso nuevo, hacer las cuentas, relacionar dos conceptos que "
        "aparecen por separado, comparar, o sacar una conclusión que se deduce de "
        "lo que dicen. La regla es sobre los HECHOS (fórmulas, datos, "
        "definiciones), que deben estar en la documentación; el razonamiento que "
        "los conecta lo ponés vos. Cuando una conclusión sea tuya y no esté "
        "escrita en el material, aclaralo: «esto no está explícito en el apunte, "
        "se deduce de [n]».\n"
        "3. Citá siempre de dónde sacaste cada cosa con la referencia [n] del "
        "fragmento correspondiente.\n"
        "4. Escribís para alguien que está estudiando el tema, no para un "
        "experto: introducí cada término técnico la primera vez que lo usás.\n"
        "5. Los fragmentos vienen de PDFs y algunas fórmulas pueden estar mal "
        "transcriptas (símbolos cambiados). Si una fórmula se ve corrupta, "
        "decilo y remití al documento original en vez de reconstruirla.\n"
        "6. Si el estudiante hace una repregunta corta (\"¿y cómo funciona?\", "
        "\"dame un ejemplo\"), entendé que sigue hablando del tema anterior de la "
        "conversación.\n"
        "7. Respondé en español rioplatense. Sin relleno ni cortesías.\n\n")
        .arg(fraseAbstencion());

    s += reglasDeFormato();
    s += QStringLiteral("\n\n") + reglasDeImagenes();

    const Modo m = modoPorId(idModo);
    // Los modos flexibles priorizan entender por sobre abstenerse: el material
    // rara vez responde textualmente lo que el estudiante pregunta, y contestar
    // "no tengo información" ante una repregunta no le sirve de nada.
    if (!m.exigente) {
        s += QStringLiteral(
            "\n\nACTITUD EN ESTE MODO (conversación): tu objetivo es que el "
            "estudiante ENTIENDA. Antes de abstenerte, agotá lo que podés hacer "
            "con lo que tenés:\n"
            "- Si los fragmentos hablan del tema aunque no respondan textualmente, "
            "usalos y armá la respuesta razonando sobre ellos.\n"
            "- Si el estudiante se refiere a algo de la conversación previa "
            "(\"eso\", \"la ecuación anterior\", \"el segundo punto\"), respondé "
            "sobre eso.\n"
            "- Si sólo podés responder parcialmente, respondé la parte que podés y "
            "decí qué falta.\n"
            "- Marcá lo que sea deducción tuya: «esto no está explícito en el "
            "apunte, se deduce de [n]».\n"
            "Reservá la frase de abstención para cuando el tema esté realmente "
            "fuera del material y tampoco haya nada en la conversación.");
    }
    // El modo Plan tiene dos consignas y sólo va UNA: la que corresponde al
    // turno en que está la conversación, que decide el llamador.
    const QString consigna =
        (planConDatos && m.id == QLatin1String("plan")) ? instruccionPlanConDatos()
                                                        : m.instruccion;
    if (!consigna.isEmpty())
        s += QStringLiteral("\n\n") + consigna;

    // Cláusula de cierre. Va DESPUÉS de la consigna del modo a propósito: las
    // plantillas ("1. Alcance, 2. Sesiones…") son instrucciones de formato muy
    // concretas y, si quedan al final, el modelo las completa aunque no tenga
    // material. Se vio con "/plan/ ¿cómo hago milanesas?": decía la frase de
    // abstención y a continuación armaba el plan igual.
    s += QStringLiteral(
        "\n\n--- REGLA QUE MANDA SOBRE TODO LO ANTERIOR ---\n"
        "Si no tenés material suficiente para responder, tu respuesta es "
        "ÚNICAMENTE esta frase, sola, sin nada antes ni después:\n"
        "\"%1\"\n"
        "En ese caso NO completes el formato del modo, NO armes secciones "
        "vacías, NO propongas temas alternativos y NO muestres ejemplos. Un "
        "formato lleno de \"no hay información\" es peor que una sola frase "
        "honesta. Abstenerse es una respuesta válida y completa.")
        .arg(fraseAbstencion());
    return s;
}

QString usuario(const QString &pregunta, const QVector<StudiaFragmento> &frags,
                const QVector<Turno> &historial, const QString &idModo,
                bool planConDatos)
{
    QString out;

    if (!historial.isEmpty()) {
        out += QStringLiteral("### Conversación previa (contexto, NO es documentación)\n\n");
        for (const Turno &t : historial) {
            const QString quien = (t.rol == QLatin1String("usuario"))
                                      ? QStringLiteral("Estudiante") : QStringLiteral("Vos");
            out += QStringLiteral("%1: %2\n\n").arg(quien, t.contenido.trimmed());
        }
    }

    out += QStringLiteral("### Fragmentos de la documentación académica\n\n");
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
    // El formato va ÚLTIMO, después de la pregunta. Es lo que el modelo tiene
    // más fresco al empezar a generar, y sin esto lo ignoraba.
    const QString formato = recordatorioDeFormato(idModo, planConDatos);
    if (!formato.isEmpty())
        out += QStringLiteral("\n") + formato + QLatin1Char('\n');
    return out;
}

bool puedeResponderDesdeConversacion(const QVector<Turno> &historial)
{
    // Hace falta al menos una respuesta previa de StudIA con contenido: es de
    // ahi de donde va a salir la respuesta.
    for (const Turno &t : historial)
        if (t.rol != QLatin1String("usuario") && t.contenido.trimmed().size() > 40)
            return true;
    return false;
}

QString usuarioSoloConversacion(const QString &pregunta,
                                const QVector<Turno> &historial,
                                const QString &idModo, bool planConDatos)
{
    QString out = QStringLiteral(
        "### Conversación hasta ahora\n\n");
    for (const Turno &t : historial) {
        const QString quien = (t.rol == QLatin1String("usuario"))
                                  ? QStringLiteral("Estudiante") : QStringLiteral("Vos");
        out += QStringLiteral("%1: %2\n\n").arg(quien, t.contenido.trimmed());
    }
    out += QStringLiteral(
        "### Situación\n"
        "Para este pedido la búsqueda en la documentación no trajo material nuevo. "
        "Puede ser porque el estudiante se refiere a algo que YA está en la "
        "conversación de arriba (\"repetí la ecuación anterior\", \"explicalo más "
        "simple\", \"no entendí el paso 2\", \"dame otro ejemplo de eso\").\n\n"
        "Respondé usando lo que ya se dijo, sin inventar hechos nuevos ni fórmulas "
        "que no aparezcan arriba. Si el pedido necesita información que no está ni "
        "en la conversación ni en el material, ahí sí decilo y sugerí cómo "
        "reformular la pregunta.\n"
        "No cites `[n]`: en este turno no hay fragmentos numerados.\n\n"
        "### Pedido del estudiante\n%1\n").arg(pregunta.trimmed());
    // Si el modo tiene formato estricto, sigue valiendo por esta rama.
    const QString formato = recordatorioDeFormato(idModo, planConDatos);
    if (!formato.isEmpty())
        out += QStringLiteral("\n") + formato + QLatin1Char('\n');
    return out;
}

Encuadre encuadrar(int propios, int existentes, int discriminantes)
{
    if (discriminantes >= kMinDiscriminantesAutonomos)
        return Encuadre::Autonoma;
    // Trae términos con contenido pero NINGUNO existe en el corpus: es un tema
    // nuevo que el material no cubre. Heredar el tema anterior sería contestar
    // otra pregunta.
    if (propios > 0 && existentes == 0)
        return Encuadre::Ajena;
    return Encuadre::Dependiente;
}

bool respondeDesdeLaConversacion(bool modoExigente, Encuadre encuadre,
                                 const QVector<Turno> &historial)
{
    // Sin una respuesta previa con contenido no hay de dónde sacar nada, por
    // más que la pregunta se apoye en lo anterior.
    if (!puedeResponderDesdeConversacion(historial))
        return false;
    if (!modoExigente)
        return true;
    // Una pregunta AJENA cambia a un tema que el corpus no cubre: ahí sí
    // corresponde abstenerse, no responderla con lo que se venía hablando.
    return encuadre == Encuadre::Dependiente;
}

QString consultaConContexto(const QString &pregunta, const QStringList &anteriores,
                            Encuadre encuadre)
{
    const QStringList propios = StudiaIndex::terminosConsulta(pregunta);
    // Se busca tal cual: o se sostiene sola, o no tiene sentido arrastrarle un
    // tema que no le corresponde.
    if (encuadre != Encuadre::Dependiente)
        return pregunta;

    QStringList terminos = propios;
    QSet<QString> vistos;
    for (const QString &t : propios)
        vistos.insert(StudiaIndex::normalizar(t));

    // Se completa con el tema de las preguntas anteriores, de la mas reciente a
    // la mas vieja, hasta llegar al tope.
    for (const QString &previa : anteriores) {
        for (const QString &t : StudiaIndex::terminosConsulta(previa)) {
            const QString clave = StudiaIndex::normalizar(t);
            if (vistos.contains(clave))
                continue;
            vistos.insert(clave);
            terminos << t;
            if (terminos.size() >= kMaxTerminosExpandida)
                return terminos.join(QLatin1Char(' '));
        }
    }
    return terminos.join(QLatin1Char(' '));
}

}  // namespace StudiaPrompt
