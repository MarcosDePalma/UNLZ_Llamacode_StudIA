#include "StudiaPrompt.h"

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
        "1) GRÁFICO DE FUNCIONES — para ver una curva, comparar dos o mostrar el "
        "área de una integral. Formato exacto:\n"
        "```grafico\n"
        "funcion: x**2 - 3*x + 2\n"
        "rango: -2, 5\n"
        "area: 1, 2\n"
        "titulo: Área bajo la parábola entre 1 y 2\n"
        "```\n"
        "Reglas: la expresión es Python/numpy en función de `x` (`**` para "
        "potencia, `sqrt`, `sin`, `cos`, `exp`, `log`, `pi`). `rango` es "
        "obligatorio. `area` es opcional y sombrea bajo la primera curva. Podés "
        "repetir `funcion:` para superponer curvas. Nada de otro código.\n"
        "Ejemplo de uso. Pedido: «graficame x²−3x+2 entre −2 y 5 y sombreá el área "
        "entre 1 y 2». Respuesta correcta: una línea de contexto y ACTO SEGUIDO el "
        "bloque de arriba, tal cual. Respuesta incorrecta: explicarle cómo "
        "graficarlo en Python o mandarlo a otro programa.\n\n"
        "2) DIAGRAMA — para procesos, clasificaciones o relaciones entre "
        "conceptos. Formato Mermaid:\n"
        "```mermaid\n"
        "flowchart TD\n"
        "  A[Entrada] --> B{¿Estable?}\n"
        "  B -- sí --> C[Salida]\n"
        "  B -- no --> D[Ajustar ganancia]\n"
        "```\n"
        "Usá etiquetas cortas y sin caracteres raros.");
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
        {QStringLiteral("libre"), QStringLiteral("Conversación"),
         QStringLiteral("Preguntas y respuestas sobre el material."),
         QString(), false},

        {QStringLiteral("resumen"), QStringLiteral("Resumen"),
         QStringLiteral("Condensa el tema en sus ideas principales."),
         QStringLiteral(
             "MODO RESUMEN. Entregá un resumen del tema pedido, con esta estructura:\n"
             "1. Un párrafo de apertura con la idea central en no más de 3 líneas.\n"
             "2. `## Puntos clave` con 4 a 8 viñetas, cada una autocontenida.\n"
             "3. `## Definiciones` con los términos técnicos que aparecen y su "
             "significado en una línea.\n"
             "4. `## Para tener en cuenta` con lo que suele confundirse o "
             "preguntarse en un examen, sólo si la documentación lo respalda.\n"
             "No agregues nada que no esté en los fragmentos."), true},

        {QStringLiteral("explicacion"), QStringLiteral("Explicación"),
         QStringLiteral("Desarrolla el concepto de menor a mayor dificultad."),
         QStringLiteral(
             "MODO EXPLICACIÓN. Explicá el tema de forma progresiva:\n"
             "1. `## La idea en una frase` — la intuición, sin tecnicismos.\n"
             "2. `## Cómo funciona` — el desarrollo técnico, paso a paso, "
             "introduciendo cada término la primera vez que aparece.\n"
             "3. `## Un ejemplo` — un caso concreto tomado de la documentación. "
             "Si los fragmentos no traen ninguno, decilo en vez de inventarlo.\n"
             "4. `## Errores frecuentes` — confusiones habituales, sólo si la "
             "documentación las menciona.\n"
             "Asumí que el estudiante ve el tema por primera vez."), false},

        {QStringLiteral("autoevaluacion"), QStringLiteral("Autoevaluación"),
         QStringLiteral("Arma preguntas de examen con su solución."),
         QStringLiteral(
             "MODO AUTOEVALUACIÓN. Generá una autoevaluación sobre el tema:\n"
             "- 5 preguntas, de menor a mayor dificultad, numeradas.\n"
             "- Mezclá tipos: conceptuales, de aplicación y de cálculo (si la "
             "documentación trae fórmulas o datos para calcular).\n"
             "- Después de las 5 preguntas, `## Respuestas`, con la solución de "
             "cada una y la cita `[n]` del fragmento que la respalda.\n"
             "Cada pregunta debe poder responderse SÓLO con los fragmentos dados. "
             "Si no alcanza para 5, hacé menos y aclaralo."), true},

        {QStringLiteral("flashcards"), QStringLiteral("Flashcards"),
         QStringLiteral("Tarjetas de repaso con frente y dorso."),
         QStringLiteral(
             "MODO FLASHCARDS. Generá tarjetas de estudio sobre el tema.\n"
             "Formato EXACTO, una tarjeta por bloque, sin texto adicional entre "
             "ellas:\n\n"
             "**1. Frente**\n"
             "La pregunta o el término, en una línea.\n\n"
             "**Dorso**\n"
             "La respuesta, en 1 a 3 líneas, autocontenida. `[n]`\n\n"
             "---\n\n"
             "Reglas: entre 6 y 12 tarjetas; una sola idea por tarjeta; el dorso "
             "tiene que entenderse sin haber leído las otras. Nada que no esté en "
             "los fragmentos."), true},

        {QStringLiteral("ejercicio"), QStringLiteral("Ejercicio"),
         QStringLiteral("Resuelve un problema paso a paso con el método del apunte."),
         QStringLiteral(
             "MODO EJERCICIO. El estudiante trae un problema para resolver.\n"
             "1. `## Datos` — qué se conoce y qué se pide, con unidades.\n"
             "2. `## Método` — qué procedimiento de la documentación aplica y "
             "por qué, citando `[n]`. La fórmula tiene que salir de los "
             "fragmentos: si no está, decilo y no la inventes.\n"
             "3. `## Desarrollo` — un paso por renglón, mostrando el reemplazo "
             "de valores antes de operar. Escribí cada resultado intermedio.\n"
             "4. `## Resultado` — el valor final con su unidad.\n"
             "5. `## Verificación` — un chequeo rápido (orden de magnitud, "
             "coherencia de unidades, caso límite).\n"
             "Estás aplicando el método a números nuevos: eso es correcto y es lo "
             "que se te pide. Lo que no podés es inventar la fórmula ni los datos. "
             "Si hacés una cuenta de la que no estás seguro, marcala como "
             "'verificar a mano'."), false},

        {QStringLiteral("plan"), QStringLiteral("Plan de estudio"),
         QStringLiteral("Organiza el tema en sesiones de estudio."),
         QStringLiteral(
             "MODO PLAN DE ESTUDIO. Armá un plan para preparar el tema:\n"
             "1. `## Alcance` — qué entra, según lo que hay en la documentación.\n"
             "2. `## Orden sugerido` — los subtemas en el orden en que conviene "
             "estudiarlos, justificando por qué uno depende del anterior.\n"
             "3. `## Sesiones` — una tabla con columnas Sesión | Qué estudiar | "
             "Material `[n]` | Cómo verificar que lo entendiste.\n"
             "4. `## Antes del examen` — el repaso final, en 3 o 4 viñetas.\n"
             "Dimensioná el plan según cuánto material hay; no inventes bibliografía "
             "que no aparezca en los fragmentos."), true},
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
        });
    }
    return out;
}

Modo modoPorId(const QString &id)
{
    for (const Modo &m : modos())
        if (m.id == id)
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
    const QString id = m.captured(1).toLower();
    if (modoPorId(id).id.isEmpty())
        return;                       // prefijo desconocido: se trata como texto
    if (idModo) *idModo = id;
    if (texto)  *texto  = m.captured(2).trimmed();
}

QString sistema(const QString &materia, const QString &idModo)
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
    if (!m.instruccion.isEmpty())
        s += QStringLiteral("\n\n") + m.instruccion;
    return s;
}

QString usuario(const QString &pregunta, const QVector<StudiaFragmento> &frags,
                const QVector<Turno> &historial)
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

QString usuarioSoloConversacion(const QString &pregunta, const QVector<Turno> &historial)
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
    return out;
}

QString consultaConContexto(const QString &pregunta, const QStringList &anteriores,
                            int discriminantes)
{
    const QStringList propios = StudiaIndex::terminosConsulta(pregunta);
    // Pregunta autosuficiente: tiene terminos propios que ubican el tema.
    if (discriminantes >= kMinDiscriminantesAutonomos)
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
