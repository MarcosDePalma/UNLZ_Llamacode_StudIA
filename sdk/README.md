# LlamaCode worker SDKs

Las extensiones Node/TypeScript y Python son workers supervisados, no plugins
embebidos en el proceso de autoridad. El host inicia el proceso, autentica un
nonce por activación, limita el tamaño de frames, asigna IDs de llamada una sola
vez y decide las capacidades. `stdout` queda reservado para el protocolo; los
logs van a `stderr`.

Implementaciones:

- `node/`: ESM sin dependencias, Node >=20, con declaraciones `index.d.ts`
  para plugins TypeScript.
- `python/`: Python >=3.10, biblioteca estándar.

Ambas exponen el mismo contrato: `hello`/`hello_ack`, `call`/`result`,
`cancel`, y llamadas de capacidades con handles opacos. Un worker nunca puede
convertir un pedido en una concesión: una capacidad ausente o revocada produce
`capability_denied`/`capability_revoked`.

Si el host cierra la conexión mientras una capability está pendiente, ambos
SDK despiertan la llamada con un error explícito (`worker_disconnected`); no
queda una promesa ni un thread bloqueado indefinidamente.

Self-tests:

```powershell
node sdk/node/test/self-test.mjs
python -m unittest discover -s sdk/python/tests -p 'test_*.py'
python tools/harness_sdk_smoke.py
```

El sandbox es seleccionado por el perfil `worker.sandbox`: `process` usa un
Job Object en Windows y un grupo de procesos en Unix; `strong` requiere
bubblewrap en Unix. En Windows `strong` se rechaza explícitamente porque un Job
Object no es aislamiento de red.
