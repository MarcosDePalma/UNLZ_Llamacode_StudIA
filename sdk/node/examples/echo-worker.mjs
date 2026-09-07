import { createWorker } from "../index.mjs";

createWorker({
  handlers: {
    echo(payload) {
      return { value: payload.value ?? null, lane: "node" };
    }
  }
}).run().catch(error => {
  process.stderr.write(`${error.stack || error}\n`);
  process.exitCode = 1;
});
