# Paquetes de evidencia de corridas

Historial de Tasks permite exportar un archivo JSON `llamacode.evidence.v1`.
El paquete contiene el `ownerId`, la versión de LlamaCode, la fecha UTC de
exportación y las corridas disponibles en el historial, incluyendo métricas,
reporte de tools, workflow, receipts y log.

Cada corrida agrega `evidenceSha256`, calculado sobre su registro antes de
agregar el hash. Esto permite detectar modificaciones posteriores del contenido
exportado; no constituye una firma criptográfica ni prueba de autoría.

El exportador no vuelve a ejecutar la Task, no incluye secretos y no inventa
datos que no hayan sido persistidos por el historial. La ruta headless
`exportRunEvidenceTo(ownerId, path)` está disponible para integraciones locales.

Las acciones Computer Use agregan receipts `desktopReceipt` con estado,
estrategia de grounding, snapshot asociado, hashes de payload/resultado y target.
Los detalles de campos sensibles se redactan antes de persistirlos.
