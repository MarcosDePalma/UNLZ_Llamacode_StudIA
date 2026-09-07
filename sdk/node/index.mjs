/**
 * LlamaCode supervised worker SDK.
 *
 * Workers speak only on stdout. Diagnostics belong on stderr. The host owns
 * capability admission; this package exposes opaque handles and fails closed
 * when a grant is absent, revoked, or from another activation.
 */

export const PROTOCOL = "llamacode-worker-v1";
export const DEFAULT_MAX_FRAME_BYTES = 1024 * 1024;

function assertObject(value, label = "object") {
  if (!value || typeof value !== "object" || Array.isArray(value)) {
    throw new TypeError(`${label} must be an object`);
  }
}

export function encodeFrame(body, maxFrameBytes = DEFAULT_MAX_FRAME_BYTES) {
  assertObject(body);
  if (body.protocol !== PROTOCOL) throw new Error("unsupported worker protocol");
  if (typeof body.type !== "string" || !body.type) throw new Error("worker frame has no type");
  const json = Buffer.from(JSON.stringify(body), "utf8");
  if (json.length === 0 || json.length > maxFrameBytes) {
    throw new Error("worker frame exceeds the configured limit");
  }
  const header = Buffer.alloc(4);
  header.writeUInt32BE(json.length, 0);
  return Buffer.concat([header, json]);
}

export class FrameDecoder {
  constructor(maxFrameBytes = DEFAULT_MAX_FRAME_BYTES) {
    this.maxFrameBytes = maxFrameBytes;
    this.buffer = Buffer.alloc(0);
  }

  push(chunk) {
    this.buffer = Buffer.concat([this.buffer, Buffer.from(chunk)]);
    const frames = [];
    while (this.buffer.length >= 4) {
      const size = this.buffer.readUInt32BE(0);
      if (size === 0 || size > this.maxFrameBytes) throw new Error("invalid worker frame size");
      if (this.buffer.length < 4 + size) break;
      const raw = this.buffer.subarray(4, 4 + size).toString("utf8");
      this.buffer = this.buffer.subarray(4 + size);
      const body = JSON.parse(raw);
      assertObject(body);
      if (body.protocol !== PROTOCOL) throw new Error("unsupported worker protocol");
      if (typeof body.type !== "string" || !body.type) throw new Error("worker frame has no type");
      frames.push(body);
    }
    return frames;
  }
}

export class CapabilityError extends Error {
  constructor(code, message, capability = "") {
    super(message);
    this.name = "CapabilityError";
    this.code = code;
    this.capability = capability;
  }
}

class CapabilityHandle {
  constructor(runtime, name, handle, generation) {
    this.runtime = runtime;
    this.name = name;
    this.handle = handle;
    this.generation = generation;
    Object.freeze(this);
  }

  call(operation, payload = {}) {
    return this.runtime.requestCapability(this, operation, payload);
  }
}

export class CapabilityBroker {
  constructor(runtime, snapshot = {}) {
    this.runtime = runtime;
    this.snapshot = snapshot || {};
    this.grants = this.snapshot.grants || {};
    this.generation = this.snapshot.generation || 1;
  }

  names() {
    return Object.entries(this.grants)
      .filter(([, grant]) => grant && grant.granted && grant.handle && grant.generation === this.generation)
      .map(([name]) => name)
      .sort();
  }

  has(name) {
    const grant = this.grants[name];
    return !!(grant && grant.granted && grant.handle && grant.generation === this.generation);
  }

  require(name) {
    const grant = this.grants[name];
    if (!this.has(name)) {
      const reason = grant?.reason || "denied_by_policy";
      throw new CapabilityError("capability_denied", `${name}: ${reason}`, name);
    }
    return new CapabilityHandle(this.runtime, name, grant.handle, this.generation);
  }
}

export class WorkerRuntime {
  constructor({ handlers = {}, maxFrameBytes = DEFAULT_MAX_FRAME_BYTES } = {}) {
    this.handlers = handlers;
    this.maxFrameBytes = maxFrameBytes;
    this.input = null;
    this.output = null;
    this.decoder = new FrameDecoder(maxFrameBytes);
    this.authenticated = false;
    this.nonce = "";
    this.capabilities = new CapabilityBroker(this, {});
    this.calls = new Map();
    this.pendingCapabilities = new Map();
    this.sequence = 0;
    this.writeChain = Promise.resolve();
  }

  async send(body) {
    const frame = encodeFrame({ protocol: PROTOCOL, ...body }, this.maxFrameBytes);
    this.writeChain = this.writeChain.then(() => new Promise((resolve, reject) => {
      const ok = this.output.write(frame, error => error ? reject(error) : resolve());
      if (!ok) this.output.once("drain", resolve);
    }));
    return this.writeChain;
  }

  requestCapability(handle, operation, payload) {
    if (!this.authenticated || !this.capabilities.has(handle.name)
        || handle.generation !== this.capabilities.generation) {
      return Promise.reject(new CapabilityError("capability_revoked", "capability handle is no longer valid", handle.name));
    }
    const requestId = `cap-${++this.sequence}`;
    return new Promise((resolve, reject) => {
      this.pendingCapabilities.set(requestId, { resolve, reject });
      this.send({ type: "capability_call", requestId, capability: handle.name,
        handle: handle.handle, operation, payload }).catch(error => {
        this.pendingCapabilities.delete(requestId);
        reject(error);
      });
    });
  }

  async run({ input = process.stdin, output = process.stdout } = {}) {
    this.input = input;
    this.output = output;
    for await (const chunk of input) {
      for (const frame of this.decoder.push(chunk)) await this.handle(frame);
    }
    for (const pending of this.pendingCapabilities.values()) {
      pending.reject(new Error("worker host disconnected"));
    }
    this.pendingCapabilities.clear();
  }

  async handle(frame) {
    if (frame.type === "hello") {
      if (this.authenticated) throw new Error("worker authenticated twice");
      const expected = process.env.LLAMACODE_WORKER_NONCE || "";
      if (expected && frame.nonce !== expected) throw new Error("worker nonce authentication failed");
      this.nonce = frame.nonce || "";
      this.capabilities = new CapabilityBroker(this, frame.capabilities || {});
      this.authenticated = true;
      await this.send({ type: "hello_ack", nonce: this.nonce,
        sdk: "@llamacode/harness-worker", sdkVersion: "0.1" });
      return;
    }
    if (!this.authenticated) throw new Error("worker sent data before authentication");
    if (frame.type === "capability_result") {
      const pending = this.pendingCapabilities.get(frame.requestId);
      if (!pending) return;
      this.pendingCapabilities.delete(frame.requestId);
      if (frame.ok) pending.resolve(frame.payload || {});
      else {
        const error = frame.error || {};
        pending.reject(new CapabilityError(error.code || "capability_error",
          error.message || "capability call failed"));
      }
      return;
    }
    if (frame.type === "cancel") {
      this.calls.get(frame.callId)?.abort.abort();
      return;
    }
    if (frame.type !== "call") throw new Error(`unexpected worker frame: ${frame.type}`);
    if (this.calls.has(frame.callId)) throw new Error("worker call id is already in use");
    const abort = new AbortController();
    this.calls.set(frame.callId, { abort });
    try {
      const payload = frame.payload || {};
      const operation = payload.operation || "handle";
      const handler = this.handlers[operation] || this.handlers.handle;
      if (typeof handler !== "function") {
        throw new Error(`unknown worker operation: ${operation}`);
      }
      const context = { callId: frame.callId, signal: abort.signal,
        get cancelled() { return abort.signal.aborted; }, capabilities: this.capabilities };
      const result = await handler(payload, context);
      await this.send({ type: "result", callId: frame.callId,
        payload: result && typeof result === "object" ? result : { value: result } });
    } catch (error) {
      await this.send({ type: "result", callId: frame.callId, payload: {
        error: { code: abort.signal.aborted ? "cancelled" : (error.code || "worker_error"),
          message: String(error.message || error) }
      }});
    } finally {
      this.calls.delete(frame.callId);
    }
  }
}

export function createWorker(options = {}) {
  return new WorkerRuntime(options);
}
